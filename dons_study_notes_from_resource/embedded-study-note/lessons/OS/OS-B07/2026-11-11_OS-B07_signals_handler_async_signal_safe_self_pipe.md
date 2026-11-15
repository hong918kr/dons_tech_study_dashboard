---
id: OS-B07
time: 70분
updated: 2026-11-11
---
# OS-B07 시그널: 핸들러, async-signal-safe, self-pipe

## 학습 목표

- `sigaction` 으로 핸들러를 설치하고 `sa_mask`(핸들러 실행 중 추가로 막을 시그널)와 `sa_flags`(`SA_RESTART`, `SA_NOCLDSTOP`, `SA_RESETHAND`)가 무엇을 바꾸는지 **자기 자신에게 시그널을 보내는 테스트** 로 보인다.
- 표준 시그널은 **쌓이지 않는다**(막힌 동안 2번 → 핸들러 1번, 자식 3개 종료 → SIGCHLD 1번)는 것을 확인하고, 그래서 SIGCHLD 핸들링은 `waitpid(-1, …, WNOHANG)` **루프** 여야 한다는 것을 설명한다.
- `SA_RESTART` 가 있을 때와 없을 때 blocking `read` 가 `EINTR` 로 끝나는지 재시작되는지, 그리고 `poll`/`select`/`nanosleep` 은 `SA_RESTART` 여도 `EINTR` 이라는 것을 macOS 에서 실측한다.
- 핸들러에서 `printf`/`malloc` 이 왜 위험한지 — **중단된 코드가 쥐고 있던 락** — 을 결정적 재현(mutex 3종)과 실제 libc 스트레스(교착·abort 실측 스택)로 보이고, POSIX async-signal-safe 함수만으로 로그를 남기며 `errno` 를 보존한다.
- 핸들러 → 메인 루프 전달 두 가지: `volatile sig_atomic_t` 플래그 + `sigsuspend`(검사↔잠들기 경쟁 제거), 그리고 **self-pipe trick**(non-blocking pipe 에 1바이트, 메인은 `poll`). 펌웨어의 "ISR 은 짧게, 일은 태스크에서" 와 같은 구조임을 연결한다.

## 1. 문제

"Ctrl-C 를 누르면 깔끔하게 종료하는 데몬을 짜 보세요. 핸들러에서 `printf` 하면 왜 안 되죠?" — 임베디드 리눅스 면접(데몬·init·센서 서비스)에서 자주 나오고, 펌웨어 면접의 "ISR 에서 하면 안 되는 일" 과 **같은 질문** 이다. 시그널 핸들러는 사용자 공간의 인터럽트 서비스 루틴(ISR)이다: 아무 명령어 사이에나 끼어들고, 끼어든 코드가 무엇을 쥐고 있는지 모른다 (C-M14). 그래서 규칙도 같다 — **짧게, 블록하지 말고, 공유 자료구조를 건드리지 말고, "일어났다" 만 알리고 일은 메인(태스크)에서** (RTOS-B08, RTOS-M02).

시그널(signal)은 커널이 프로세스에 보내는 비동기 알림이다. 다른 프로세스가 `kill()` 로(SIGTERM), 터미널이 Ctrl-C 로(SIGINT), 커널이 자식 종료 때(SIGCHLD) 또는 잘못된 메모리 접근 때(SIGSEGV) 보낸다. OSTEP 은 5.5절과 33장 Aside 에서 짧게만 다루고 Stevens & Rago 를 권한다 — 이 레슨이 그 빈칸을 채운다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">시그널 한 개의 일생 (프로세스당 시그널마다 "펜딩 비트" 1개)</text>
  <rect x="10" y="40" width="150" height="54" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="20" y="62" fill="currentColor">생성 (generate)</text>
  <text x="20" y="82" fill="currentColor" font-size="12">kill / Ctrl-C / 자식 종료</text>
  <rect x="200" y="40" width="160" height="54" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="210" y="62" fill="currentColor">펜딩 (pending)</text>
  <text x="210" y="82" fill="currentColor" font-size="12">비트 1개 — 두 번 와도 1</text>
  <rect x="400" y="40" width="150" height="54" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="410" y="62" fill="currentColor">막혔나? (mask)</text>
  <text x="410" y="82" fill="currentColor" font-size="12">sigprocmask / sa_mask</text>
  <rect x="590" y="40" width="140" height="54" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="600" y="62" fill="currentColor">전달 (deliver)</text>
  <text x="600" y="82" fill="currentColor" font-size="12">커널 → 사용자 복귀 때</text>
  <g stroke="currentColor" stroke-width="2" fill="none">
    <path d="M160 67 L198 67"/><path d="M190 61 L198 67 L190 73"/>
    <path d="M360 67 L398 67"/><path d="M390 61 L398 67 L390 73"/>
    <path d="M550 67 L588 67"/><path d="M580 61 L588 67 L580 73"/>
    <path d="M475 94 L475 120 L280 120 L280 96"/><path d="M274 104 L280 96 L286 104"/>
  </g>
  <text x="300" y="138" fill="currentColor" font-size="12">막혀 있으면 펜딩에 머문다 (풀리는 순간 전달)</text>
  <rect x="10" y="160" width="720" height="150" rx="6" fill="none" stroke="currentColor"/>
  <text x="20" y="182" fill="currentColor" font-weight="bold">전달 = 지금 하던 일 위에 핸들러 호출을 "얹는다"</text>
  <rect x="20" y="195" width="200" height="40" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="30" y="220" fill="currentColor">main: printf(...) 도중</text>
  <rect x="250" y="195" width="210" height="40" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="260" y="220" fill="currentColor">_sigtramp → handler(sig)</text>
  <rect x="490" y="195" width="230" height="40" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="500" y="220" fill="currentColor">sigreturn → 하던 곳으로 복귀</text>
  <g stroke="currentColor" stroke-width="2" fill="none">
    <path d="M220 215 L248 215"/><path d="M240 209 L248 215 L240 221"/>
    <path d="M460 215 L488 215"/><path d="M480 209 L488 215 L480 221"/>
  </g>
  <text x="20" y="260" fill="currentColor">핸들러가 도는 동안: 그 시그널 자신 + sa_mask 가 자동으로 막힌다 (= 같은 우선순위 ISR 은 중첩 안 됨)</text>
  <text x="20" y="282" fill="currentColor">blocking 시스템 콜 도중이었다면: SA_RESTART 면 재시작, 아니면 -1 / errno = EINTR</text>
  <text x="20" y="302" fill="currentColor">펌웨어 대응: 펜딩 비트 = NVIC ISPR, 마스크 = PRIMASK/BASEPRI, 전달 = 예외 진입 (C-M14, BM-M01)</text>
</svg>
```

### Part A — sigaction, 합쳐짐, EINTR

- `int install(int sig, void (*fn)(int), int flags, const sigset_t *mask)` — `struct sigaction` 을 0 으로 채우고 핸들러·`sa_mask`(NULL 이면 빈 집합)·`sa_flags` 를 넣어 `sigaction()`.
- `ssize_t read_eintr(int fd, void *buf, size_t n)` — `-1/EINTR` 이면 다시 부르는 래퍼.
- 테스트(전부 `raise`/`kill(getpid())` 로 자신에게): 설치·조회, 막힌 동안 SIGUSR1 ×2 → 펜딩 1 → 핸들러 1번, `sa_mask` 유무에 따른 핸들러 중첩 순서(`<{}>` vs `<>{}`), 그리고 200 ms 뒤에야 데이터가 오는 pipe 를 `read` 하는 도중 30 ms 에 SIGALRM: `SA_RESTART` 없으면 `EINTR`, 있으면 재시작, 래퍼는 어느 쪽이든 1바이트.

### Part B — async-signal-safe 로그

- `size_t fmt_ulong(char *dst, unsigned long v)` — `snprintf` 없이 10진 문자열로.
- `void log_handler(int sig)` — `"signal <번호> #<순번>\n"` 을 스택 버퍼에 조립해 **`write()` 한 번** 으로. `errno` 를 저장했다가 복원한다. 테스트: 로그 fd 가 닫혀 있어 `write` 가 `EBADF` 로 실패해도 메인의 `errno`(ERANGE)가 그대로여야 한다.
- 왜 `printf` 가 안 되는지는 `demo_unsafe_handler.c` 로 **실제로** 본다 (5. 풀이).

### Part C — 메인 루프로 이벤트 넘기기: 플래그 vs self-pipe

- `int wait_flag(int sig)` — `volatile sig_atomic_t g_flag` 가 설 때까지 기다린다. 하네스는 "플래그 검사 직후, 잠들기 직전" (`race_window()`)에 시그널을 넣는다. 하네스의 깨진 버전 `while(!flag) pause();` 은 거기서 **영원히** 잔다(워치독이 죽임). 내 버전은 `sigprocmask` + `sigsuspend` 로 경쟁을 없앤다.
- `int selfpipe_setup(void)` — pipe 두 끝 모두 `O_NONBLOCK | FD_CLOEXEC`, SIGINT/SIGTERM/SIGCHLD 에 `SA_RESTART`(+SIGCHLD 는 `SA_NOCLDSTOP`), `sa_mask` = 세 시그널.
- `void selfpipe_handler(int sig)` — errno 보존 + 1바이트 `write`. pipe 가 꽉 차도(EAGAIN) 막히지 않는다.
- `int loop_once(int timeout_ms, loop_t *L)` — `poll` → `EAGAIN` 까지 drain → SIGTERM/SIGINT 는 `L->stop`, SIGCHLD 는 `waitpid(-1, &st, WNOHANG)` 루프로 **전부** 회수.
- 테스트: SIGCHLD 를 막아 둔 채 자식 3개가 모두 좀비가 되게 한 뒤 풀면 **바이트 1개, 회수 3개, 남은 좀비 0**. 아무도 안 읽는 pipe 에 10만 번 raise 해도 핸들러가 막히지 않음(자식에서, 워치독). `poll` 도중 시그널(EINTR)을 에러로 보지 않음.

## 2. Problem (English)

> **Signal-safe event delivery for a small daemon (POSIX, single-threaded).**
>
> ```c
> int     install(int sig, void (*fn)(int), int flags, const sigset_t *mask);
> ssize_t read_eintr(int fd, void *buf, size_t n);
> size_t  fmt_ulong(char *dst, unsigned long v);      // async-signal-safe itoa
> void    log_handler(int sig);                       // "signal N #k\n" with ONE write(); preserve errno
> int     wait_flag(int sig);                         // race-free wait for a volatile sig_atomic_t flag
> int     selfpipe_setup(void);                       // non-blocking, close-on-exec pipe; SIGINT/TERM/CHLD
> void    selfpipe_handler(int sig);                  // write 1 byte, never block, preserve errno
> int     loop_once(int timeout_ms, loop_t *L);       // poll + drain; TERM/INT -> stop; CHLD -> reap all
> ```
>
> Constraints: handlers may only call async-signal-safe functions (POSIX 2.4.3). No `printf`, `malloc`, or locks in handlers. Standard signals do not queue: two identical signals while blocked are delivered once, and several children may exit for a single SIGCHLD. `poll()` may fail with `EINTR`; that is not an error.
>
> Example: block SIGCHLD, fork three children that `_exit(1)`, `_exit(2)`, `_exit(3)`, wait until all are zombies, unblock → exactly one byte arrives in the self-pipe, and one `loop_once` reaps all three with their exit codes; afterwards `waitpid(-1, NULL, WNOHANG)` fails with `ECHILD`.
>
> Follow-ups: Why can `printf` in a handler deadlock? What does `SA_RESTART` restart, and what does it not? Why must the self-pipe be non-blocking? Why save `errno`? What is wrong with `if (!flag) pause();`? How would you do this in a multi-threaded program (sigwait / signalfd)? What is the firmware equivalent?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력, 약 1 초, 멈추지 않는다 — 일부 생략):

```text
FAIL starter.c:256  install(SIGUSR1, h_count, 0, NULL) == 0
...
C-1 flag race: pause() version=HUNG(watchdog), wait_flag()=exit 1
SKIP 8 self-pipe checks (Part C): pipe missing or read end blocking (a drain would hang)
3/34 passed
```

통과하는 3개는 하네스가 보여 주는 사실이다: 막힌 동안 raise 하면 펜딩이 선다, 아무것도 안 하는 핸들러는 errno 를 안 바꾼다, **깨진 `pause()` 버전은 정말 멈춘다** (HUNG). 하네스 설계:

- 시그널은 전부 **자기 자신에게** (`raise` 는 리턴 전에 핸들러가 끝난다. `kill(getpid())` 도 단일 스레드에서는 리턴 전에 최소 1개가 전달된다 — POSIX `kill`). 그래서 결과가 매번 같다.
- 하네스는 시작할 때 SIGUSR1/2·ALRM·TERM·INT 에 자기 핸들러(`h_fallback`)를 깐다. 학생 `install()` 이 실패해도 기본 동작(프로세스 종료)으로 하네스가 죽지 않는다. Part C 는 하네스 전용 `hx_sigaction()` 을 써서 Part A 실패가 섞이지 않게 했다.
- 멈출 수 있는 것(깨진 pause, 꽉 찬 pipe 에 쓰는 핸들러)은 `run_in_child()` 로 자식에서 돌리고 부모가 `waitpid(WNOHANG)` 로 보다가 시간 초과면 `SIGKILL` → `HUNG`. self-pipe 읽기 끝이 blocking 이면 drain 이 멈추므로 Part C 를 SKIP 한다.
- macOS 시그널 번호: SIGUSR1=30, SIGUSR2=31 (Linux 는 10, 12). `kill -l` 로 확인. 코드에서는 항상 이름을 쓴다.

관찰용 파일 (하네스와 별개):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_restart.c -o demo_restart && ./demo_restart          # 어떤 호출이 재시작되나
cc -std=c11 -Wall -Wextra -Werror -O2 demo_unsafe_handler.c -o demo_unsafe_handler && ./demo_unsafe_handler   # 교착/abort 실측
cc -std=c11 -Wall -Wextra -Werror -O2 demo_daemon.c -o demo_daemon
./demo_daemon & sleep 1.5; kill -TERM $!; wait $!; echo "daemon exit=$?"                        # self-pipe 감시 데몬
```

`demo_unsafe_handler` 는 자식들이 일부러 죽으므로 `~/Library/Logs/DiagnosticReports/` 에 크래시 리포트(`.ips`)가 남는다 — 아래에서 그 내용을 읽는다.

## 4. 힌트

<details>
<summary>힌트 1 — sigaction 은 "구조체를 0 으로, 세 필드, 호출" 이 전부</summary>

`memset(&sa, 0, sizeof sa)` 를 빼먹으면 쓰레기 `sa_flags`(예: `SA_RESETHAND`, `SA_SIGINFO`)가 들어갈 수 있다. `sa_mask` 는 `sigemptyset` 으로 초기화하거나 받은 집합을 복사한다. 이 함수에 `signal()` 을 쓰지 않는 이유: `signal()` 의 의미(재시작 여부, 한 번 뒤 기본값으로 되돌리기)는 시스템마다 달랐다. macOS 의 `signal(3)` 은 `SA_RESTART` 를 켠다 (실측 `sa_flags=0x2`).

</details>

<details>
<summary>힌트 2 — EINTR 은 "다시 해" 이지 에러가 아니다</summary>

`do { r = read(...); } while (r < 0 && errno == EINTR);` — 다른 에러와 0(EOF)은 그대로 돌려준다. 일부만 읽힌 뒤 시그널이 오면 `EINTR` 이 아니라 **짧은 읽기(short count)** 가 온다 (연습 3 의 `write` 와 같다).

</details>

<details>
<summary>힌트 3 — 핸들러 안의 itoa: 아래 자리부터 모으고 뒤집기</summary>

`v % 10` 을 임시 배열에 쌓고(`do … while (v)` 로 0 도 처리), 거꾸로 복사. `unsigned long` 최댓값은 20자리. 문자열 상수 `"signal "` 복사도 `strcpy` 대신 루프로 하면 목록 걱정이 없다. `errno` 는 맨 처음 지역 변수에 저장, 맨 끝에 복원.

</details>

<details>
<summary>힌트 4 — wait_flag: 검사할 때는 막고, 잠드는 순간에만 연다</summary>

`while (!flag) pause();` 의 틈: 검사 후 `pause()` 전에 시그널이 오면 핸들러가 flag 를 세우지만 `pause()` 는 **다음** 시그널을 기다린다. 해결: 먼저 `sigprocmask(SIG_BLOCK)` 으로 막는다 → 그동안 온 시그널은 펜딩에 남는다 → `sigsuspend(&old)` 가 "마스크를 old 로 바꾸기 + 잠들기" 를 **원자적으로** 하므로 펜딩이 있으면 바로 전달되고 돌아온다. 끝나면 마스크를 원래대로.

</details>

<details>
<summary>힌트 5 — self-pipe: 양 끝 non-blocking, 핸들러는 실패를 무시, 루프는 끝까지 drain</summary>

쓰기 끝이 blocking 이면 pipe(64 KB)가 꽉 찼을 때 핸들러가 `write` 에서 영원히 잔다 — 그런데 pipe 를 비울 메인 루프는 그 핸들러 **밑에** 깔려 있다. 읽기 끝이 blocking 이면 "EAGAIN 까지 drain" 이 마지막에 멈춘다. SIGCHLD 바이트 하나가 자식 여럿을 뜻할 수 있으므로 `while (waitpid(-1, &st, WNOHANG) > 0)`. `poll` 이 `-1/EINTR` 이면 0 을 돌려주고 다음 바퀴에.

</details>

## 5. 풀이

### Part A — sigaction 과 그 플래그

```c
static int install(int sig, void (*fn)(int), int flags, const sigset_t *mask) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = fn;
    if (mask) sa.sa_mask = *mask; else sigemptyset(&sa.sa_mask);
    sa.sa_flags = flags;
    return sigaction(sig, &sa, NULL);
}
```

| 필드/플래그 | 뜻 | 이 레슨에서 |
|---|---|---|
| `sa_mask` | 핸들러가 도는 동안 **추가로** 막을 시그널 (그 시그널 자신은 기본으로 막힘) | A-3, self-pipe 세 시그널 서로 막기 |
| `SA_RESTART` | 핸들러가 끝난 뒤 "느린" 시스템 콜(pipe/tty/소켓 read·write, wait)을 재시작 | A-4, demo_restart |
| `SA_NOCLDSTOP` | 자식이 **멈출** 때(SIGSTOP/SIGTSTP)는 SIGCHLD 를 보내지 않음 — 종료만 | self-pipe SIGCHLD |
| `SA_RESETHAND` | 한 번 전달되면 기본 동작으로 되돌림 (옛 System V `signal()` 의미) | 연습 2 (두 번째 Ctrl-C) |
| `SA_NODEFER` | 그 시그널 자신도 막지 않음 → 같은 핸들러가 중첩될 수 있음 | 쓰지 말 것 |
| `SA_SIGINFO` | `sa_sigaction(int, siginfo_t *, void *)` 로 보낸 pid·종료 코드 등을 받음 | Q&A |
| `SA_NOCLDWAIT` | 자식이 좀비를 남기지 않음 (`SIGCHLD` 를 `SIG_IGN` 해도 macOS 에서 같은 효과 — 실측 `waitpid → ECHILD`) | Q&A |

**합쳐짐 (coalescing).** 펜딩은 시그널마다 **비트 하나** 다. 실제 출력:

```text
A-2 coalescing: raised 2x while blocked -> pending=1, handler ran 1 time(s) after unblock
```

POSIX 는 표준 시그널이 여러 번 펜딩될 때 쌓을지를 구현에 맡기는데(`sigqueue` 로 큐잉되는 실시간 시그널은 별개), macOS 는 합친다. macOS SDK 에는 `SIGRTMIN` 도, `sigqueue`/`sigtimedwait`/`sigwaitinfo` 선언도 없다(헤더 확인, `NSIG=32`). 결론: **시그널은 "개수" 를 전달하지 못한다.** 개수가 중요하면 상태(카운터, 큐)를 따로 둔다 — 원리는 C-M14 의 tick 플래그 vs 원자 카운터와 같다.

**sa_mask 와 중첩.** USR1 핸들러가 안에서 `raise(SIGUSR2)` 를 한다. `<` `>` 는 USR1 핸들러 시작·끝, `{` `}` 는 USR2 핸들러:

```text
A-3 sa_mask={}     : <{}>        USR2 가 USR1 핸들러 한가운데서 실행 (중첩)
A-3 sa_mask={USR2} : <>{}        USR2 는 펜딩 → USR1 핸들러가 끝나며 마스크가 풀릴 때 실행
```

중첩되면 두 핸들러가 같은 전역을 만지는 순간 핸들러끼리 레이스가 생긴다. self-pipe 의 세 핸들러에 서로를 `sa_mask` 로 넣는 이유다. Cortex-M 으로 옮기면 "같은 우선순위 ISR 끼리는 선점하지 않는다 / BASEPRI 로 낮은 우선순위를 막는다" 와 같은 설계 손잡이다 (BM-M01).

**SA_RESTART vs EINTR.** `demo_restart.c` 실제 출력을 표로 (각 호출은 30 ms 에 SIGALRM, 막힘은 200~300 ms 뒤 저절로 풀림. 첫 줄: `signal(3) on this OS installs with sa_flags=0x2 (SA_RESTART=0x2) -> BSD semantics`):

| 호출 (막힌 상태) | flags = 0 | flags = SA_RESTART |
|---|---|---|
| `read(pipe, 1)` | -1 EINTR, 37 ms | **1, 201 ms (재시작)** |
| `write(꽉 찬 pipe, 1)` | -1 EINTR, 38 ms | **1, 203 ms (재시작)** |
| `waitpid(child)` | -1 EINTR, 37 ms | **pid, 203 ms (재시작)** |
| `poll(300 ms)` | -1 EINTR, 39 ms | -1 EINTR, 39 ms |
| `select(300 ms)` | -1 EINTR, 38 ms | -1 EINTR, 36 ms |
| `nanosleep(300 ms)` | -1 EINTR, 37 ms (rem 262 ms) | -1 EINTR, 39 ms (rem 261 ms) |

읽는 법:

- `read`/`write`/`waitpid` 는 `SA_RESTART` 면 재시작돼서 200 ms 에 정상 결과. macOS `sigaction(2)` 맨 페이지도 재시작 대상을 "communications channel or a slow device" 의 `read`/`write`/`sendto`/`recvfrom`/… 와 `wait` 로 적는다.
- `poll`/`select`/`nanosleep` 은 `SA_RESTART` 여도 **EINTR** (macOS 실측). 타임아웃이 있는 대기는 재시작하면 시간 의미가 애매해지기 때문이다. 리눅스 `signal(7)` 도 이들을 "never restarted" 목록에 둔다. 그래서 이벤트 루프의 `poll` 은 **반드시** EINTR 을 처리한다 (C-8).
- 하네스 A-4 실제 출력도 같다: 재시작 없음 `ret=-1 EINTR after 39 ms`, `SA_RESTART` `ret=1 after 202 ms`, `read_eintr` 는 플래그와 상관없이 `ret=1 after 201 ms`.

`SA_RESTART` 를 켤지 말지는 설계 선택이다. 켜면 라이브러리 코드가 EINTR 에 덜 놀라지만, **blocking `read` 에 묶인 메인이 SIGTERM 을 영영 못 볼 수 있다** (핸들러가 플래그만 세우고 read 는 재시작돼 계속 잠듦). 그래서 종료 처리는 플래그만으로 하지 말고 self-pipe 처럼 **대기 자체를 깨우는 경로** 를 만든다.

### Part B — 왜 핸들러에서 printf/malloc 이 위험한가

핸들러는 **아무 명령어 사이에나** 끼어든다. 끼어든 코드가 `printf` 안에서 `FILE` 락을 쥐고 있었다면, 핸들러의 `printf` 는 그 락을 기다린다. 그런데 락을 풀 코드(메인)는 핸들러가 리턴해야 다시 돈다 — **자기 자신을 기다리는 교착** 이다. 락이 재귀 락이면 교착 대신 **반쯤 고쳐진 자료구조** 를 핸들러가 만진다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">한 스레드 안의 교착: 핸들러는 자신이 끼어든 코드를 기다린다</text>
  <text x="10" y="55" fill="currentColor">main</text>
  <rect x="70" y="38" width="120" height="28" class="f-mut" stroke="currentColor"/>
  <text x="80" y="57" fill="currentColor">printf(...)</text>
  <rect x="190" y="38" width="170" height="28" class="f-warn" stroke="currentColor"/>
  <text x="200" y="57" fill="currentColor">FILE 락 보유 중</text>
  <rect x="360" y="38" width="370" height="28" fill="none" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="370" y="57" fill="currentColor">멈춤 — 핸들러가 리턴해야 다시 돈다</text>
  <path d="M360 70 L360 100" stroke="currentColor" stroke-width="2"/>
  <path d="M354 92 L360 100 L366 92" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="370" y="92" fill="currentColor" font-size="12">SIGALRM 도착</text>
  <text x="10" y="128" fill="currentColor">handler</text>
  <rect x="360" y="110" width="110" height="28" class="f-mut" stroke="currentColor"/>
  <text x="370" y="129" fill="currentColor">printf(...)</text>
  <rect x="470" y="110" width="260" height="28" class="f-warn" stroke="currentColor"/>
  <text x="480" y="129" fill="currentColor">flockfile → 같은 락 대기 … 영원히</text>
  <path d="M600 110 C600 84 280 84 275 66" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="480" y="153" fill="currentColor" font-size="12">점선: 기다리는 락의 주인 = 멈춰 있는 main</text>
  <rect x="10" y="160" width="720" height="78" rx="5" fill="none" stroke="currentColor"/>
  <text x="20" y="182" fill="currentColor">락 종류에 따라 증상이 다르다 (demo_unsafe_handler 실측):</text>
  <text x="20" y="204" fill="currentColor">일반 mutex → 교착 · os_unfair_lock (libmalloc, locale) → 재귀 감지 후 abort</text>
  <text x="20" y="226" fill="currentColor">재귀 mutex → 락은 통과, 그러나 main 이 고치던 중인 버퍼를 핸들러가 덮어씀 (조용한 손상)</text>
</svg>
```

**① 결정적 재현 — `demo_unsafe_handler mutex`.** "스레드 안전" 로거(stdio 의 `FILE` 처럼: 락 → `memcpy` → 길이 갱신 → 해제)를 메인이 쓰는 도중, `memcpy` 와 길이 갱신 **사이** 에서 `raise(SIGUSR1)` 하고 핸들러도 같은 로거를 부른다. 실제 출력:

```text
[mutex NORMAL]
   1027 ms  NO PROGRESS for 1 s (deadlock)  main_it=0 handler_runs=0  -> SIGKILL pid 27401
[mutex ERRORCHECK]
    handler's pthread_mutex_lock -> Resource deadlock avoided
    main: log_append returned. loglen=6 buf="hello "  (expected "hello SIG" or "SIGhello ")
[mutex RECURSIVE]
    main: log_append returned. loglen=9 buf="SIGlo ..."  (expected "hello SIG" or "SIGhello ")
```

- NORMAL: 매번 교착 (macOS 의 기본 mutex 는 같은 스레드의 재진입을 감지하지 않는다).
- ERRORCHECK: `EDEADLK` 로 감지 — 핸들러의 로그가 그냥 사라진다.
- RECURSIVE: 가장 나쁘다. 핸들러가 `loglen=0` 위치에 "SIG" 를 써서 메인의 "hel" 을 덮고, 메인은 길이를 6 더해 끝에 쓰레기 3바이트(`...` = NUL)가 붙는다. **크래시도 경고도 없다.**

**② 진짜 libc — `demo_unsafe_handler printf|malloc`.** 100 µs 마다 SIGALRM, 메인 루프와 핸들러가 둘 다 `printf`(또는 `malloc(100000)/free`) 를 부른다. 자식 5개씩, 실제 출력:

```text
[printf in handler, SIGALRM every 100 us, main loop also calls printf]
    250 ms  died by signal 9 (Killed: 9)   main_it=106701 handler_runs=2137
    257 ms  died by signal 9 (Killed: 9)   main_it=101086 handler_runs=2172
    253 ms  died by signal 9 (Killed: 9)   main_it=14944 handler_runs=319
   1274 ms  NO PROGRESS for 1 s (deadlock)  main_it=88104 handler_runs=1921  -> SIGKILL pid 27454
    258 ms  died by signal 9 (Killed: 9)   main_it=233 handler_runs=4
[malloc(100000) in handler, SIGALRM every 100 us, main loop also calls malloc(100000)/free]
    255 ms  died by signal 9 (Killed: 9)   main_it=2217 handler_runs=1
    255 ms  died by signal 9 (Killed: 9)   main_it=651 handler_runs=0
    ...(5/5 같은 모양)
```

`died by signal 9` 는 데모가 아니라 **시스템이** 죽인 것이다 (데모의 SIGKILL 은 "NO PROGRESS" 줄에서만). 크래시 리포트(`~/Library/Logs/DiagnosticReports/OS-B07_du-*.ips`)를 열면 매번 같다 — `EXC_BREAKPOINT`, 종료 시그널 SIGKILL, 그리고:

```text
BUG IN CLIENT OF LIBPLATFORM: Trying to recursively lock an os_unfair_lock
_os_unfair_lock_recursive_abort ← _os_unfair_lock_lock_slow    (재귀 잠금 감지 → 일부러 죽임)
  ← xzm_segment_group_alloc_chunk ← _xzm_malloc_large_huge ← h_malloc   (핸들러의 malloc)
  ← _sigtramp                                                           (여기서 끼어듦)
  ← mach_vm_reclaim_try_cancel ← _xzm_reclaim_mark_used_locked ← … ← xzm_segment_group_free_chunk
  ← stress ← main                                     (메인은 free() 안에서 같은 락을 쥔 채)
```

malloc 실행 5개 중 3개는 메인이 `free` 안(`xzm_segment_group_free_chunk`), 2개는 `malloc` 안(`xzm_segment_group_alloc_chunk`)에서 중단됐다. printf 실행 4개는 메인의 `printf → __vfprintf → localeconv_l` 이 쥔 os_unfair_lock 을 핸들러의 `printf → … → localeconv_l` 이 다시 잡으려다 같은 식으로 죽었다. 교착이 난 printf 실행은 `sample <pid> 1` 로 떠 본 스택(직접 실행한 것, 요약):

```text
main → printf → vfprintf_l → funlockfile          ← 메인은 FILE 락을 푸는 도중에 중단됨
  → _sigtramp → hprintf → printf → vfprintf_l → flockfile
    → _pthread_mutex_firstfit_lock_wait → __psynch_mutexwait   ← 영원히 (STAT=S, CPU 0%)
```

정리하면 macOS 에서는 "가끔 교착, 대개 abort". 어느 쪽이든 **운에 달린 버그** 이고, 다른 libc(glibc, newlib)에서는 증상이 또 다르다 (이 레슨은 macOS 에서만 실행했다). 같은 이유로 `malloc` 을 부르는 모든 것(`printf` 일부 경로, `strdup`, `syslog`, `exit()` 의 atexit 처리)이 위험하다. 핸들러에서 끝내야 하면 `exit()` 가 아니라 `_exit()`.

**async-signal-safe 목록.** POSIX 2.4.3 은 핸들러에서 불러도 되는 함수 표를 정해 둔다. macOS `man 2 sigaction` 에도 목록이 있다: `_exit, close, dup2, fcntl, fork, getpid, kill, pipe, raise, read, sigaction, sigprocmask, sigsuspend, sleep, waitpid, write, …`, 그리고 문자열은 `strcpy/strcat/strncpy/strncat/strlcpy/strlcat` 정도. `printf`, `malloc`, `free`, `pthread_mutex_lock` 은 없다. 같은 맨 페이지가 "errno 를 복사해 두었다가 복원하라" 고 권한다.

**errno 보존.** 메인이 `read` 실패 직후 `errno` 를 보려는 찰나 핸들러가 끼어들어 `write` 가 실패하면, 메인은 엉뚱한 errno 를 본다. 테스트는 로그 fd 를 닫아 둔 채(`EBADF`) `errno = ERANGE; raise(...)` 후 ERANGE 인지 본다. `log_handler`:

```c
int saved = errno;                                     /* ① 저장 */
/* buf 에 "signal " (strcpy 대신 루프) + fmt_ulong(sig) + " #" + fmt_ulong(count) + '\n' */
(void)!write(g_log_fd, buf, n);                        /* ② 한 번에 — 줄이 섞이지 않게 */
errno = saved;                                         /* ③ 복원 */
```

`write` 를 **한 번** 하는 이유: pipe 에 대한 `PIPE_BUF` 이하 쓰기는 원자적이다 (POSIX `write`, macOS `getconf PIPE_BUF` = 512). 여러 번 나눠 쓰면 메인의 출력과 줄이 섞인다. 실제 출력: `B log pipe got: "signal 30 #1|signal 31 #2|"` (`|` = 줄바꿈).

솔직한 각주: 테스트는 `raise` 로 보내므로 핸들러가 `g_log_count` 를 **읽어도** 정의된 동작이다. 진짜 비동기 시그널에서는 C11 7.14.1.1p5 와 POSIX 2.4.3 이 핸들러에서 정적 객체를 "`volatile sig_atomic_t` 에 대입" 하는 것 외에는(+ C11 의 lock-free 원자 객체) 정의하지 않는다. 카운터가 필요하면 lock-free `atomic_int` + `atomic_fetch_add` (C-M14 Part A). 핸들러가 `g_log_fd`·`g_sp[1]` 같은 정적 fd 를 읽는 것도 문구상으로는 같은 범주지만, "설치 전에 한 번 쓰고 이후 안 바뀌는 값을 읽기" 는 self-pipe 의 표준 관용구이고 모든 실용 구현에서 동작한다. 엄격히 하려면 `_Atomic int` 로 둔다.

### Part C — (1) 플래그와 sigsuspend

```c
static int wait_flag(int sig) {
    sigset_t blk, old;
    sigemptyset(&blk); sigaddset(&blk, sig);
    if (sigprocmask(SIG_BLOCK, &blk, &old) != 0) return -1;   /* ① 검사 동안 막는다 */
    while (!g_flag) {
        race_window();                                        /* 테스트: 여기서 raise → 펜딩 */
        sigsuspend(&old);                                     /* ② 마스크 복원 + 잠들기 = 원자적 */
    }
    g_flag = 0;
    sigprocmask(SIG_SETMASK, &old, NULL);                     /* ③ 원래 마스크 */
    return 0;
}
```

| 시점 | `while(!flag) pause();` (깨진 버전) | `sigsuspend` 버전 |
|---|---|---|
| 검사: flag == 0 | 진행 | 진행 (sig 는 막혀 있음) |
| 이 틈에 시그널 | **즉시 핸들러** → flag = 1 | **펜딩** 으로 남음 |
| 잠들기 | `pause()` — 이미 지나간 시그널은 모름 → **영원히** | `sigsuspend(old)` — 마스크가 풀리는 순간 펜딩이 전달 → 핸들러 → 리턴 |
| 실측 (C-1) | `HUNG(watchdog)` | `OK` |

이것은 OS-B06 의 lost wakeup(락 없이 검사 후 `cond_wait`)과 **같은 버그** 이고, 베어메탈의 "플래그 검사 → `WFI`" 경쟁과도 같다. Cortex-M 해법도 같은 모양이다: 인터럽트를 막고(PRIMASK) 검사 → `WFI` (PRIMASK 가 서 있어도 펜딩 인터럽트는 WFI 를 깨운다) → 풀기 (BM-M11).

### Part C — (2) self-pipe trick

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">self-pipe: 시그널을 "읽을 수 있는 fd" 로 바꾼다</text>
  <rect x="10" y="45" width="190" height="90" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="20" y="67" fill="currentColor">selfpipe_handler(sig)</text>
  <text x="20" y="89" fill="currentColor" font-family="monospace" font-size="12">e = errno;</text>
  <text x="20" y="107" fill="currentColor" font-family="monospace" font-size="12">write(sp[1], &amp;sig, 1);</text>
  <text x="20" y="125" fill="currentColor" font-family="monospace" font-size="12">errno = e;</text>
  <rect x="250" y="55" width="220" height="70" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="260" y="77" fill="currentColor">커널 pipe 버퍼 (≈64 KB)</text>
  <text x="260" y="97" fill="currentColor" font-size="12">바이트가 "기억" 된다 → 경쟁 없음</text>
  <text x="260" y="115" fill="currentColor" font-size="12">꽉 차면 EAGAIN (O_NONBLOCK)</text>
  <rect x="520" y="45" width="210" height="90" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="530" y="67" fill="currentColor">메인 루프</text>
  <text x="530" y="89" fill="currentColor" font-family="monospace" font-size="12">poll(sp[0], ...)</text>
  <text x="530" y="107" fill="currentColor" font-family="monospace" font-size="12">EAGAIN 까지 read</text>
  <text x="530" y="125" fill="currentColor" font-family="monospace" font-size="12">바이트별 dispatch</text>
  <g stroke="currentColor" stroke-width="2" fill="none">
    <path d="M200 90 L248 90"/><path d="M240 84 L248 90 L240 96"/>
    <path d="M470 90 L518 90"/><path d="M510 84 L518 90 L510 96"/>
  </g>
  <rect x="10" y="160" width="230" height="56" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="20" y="182" fill="currentColor">SIGTERM / SIGINT</text>
  <text x="20" y="202" fill="currentColor" font-size="12">stop = sig → 정리 후 종료</text>
  <rect x="255" y="160" width="230" height="56" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="265" y="182" fill="currentColor">SIGCHLD (1바이트 = 자식 ≥1)</text>
  <text x="265" y="202" fill="currentColor" font-size="12">while (waitpid(-1,…,WNOHANG) &gt; 0)</text>
  <rect x="500" y="160" width="230" height="56" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="510" y="182" fill="currentColor">다른 fd (소켓, 센서, 타이머)</text>
  <text x="510" y="202" fill="currentColor" font-size="12">같은 poll 에 함께 등록</text>
  <text x="10" y="245" fill="currentColor">핸들러는 1바이트만 — 락·malloc·printf 없음. 모든 일은 메인 루프 (일반 코드) 에서.</text>
  <text x="10" y="267" fill="currentColor">= ISR 이 큐/세마포어에 알리고 태스크가 처리하는 구조 (RTOS-B08, RTOS-M02)</text>
  <text x="10" y="289" fill="currentColor">Linux 는 signalfd (OS-M18), macOS/BSD 는 kqueue EVFILT_SIGNAL (OS-A11) 이 같은 일을 커널에서</text>
</svg>
```

**`selfpipe_setup` 의 각 줄이 막는 사고:**

| 설정 | 빼먹으면 | 하네스 |
|---|---|---|
| 쓰기 끝 `O_NONBLOCK` | 메인이 바쁜 동안 시그널 폭주 → pipe 꽉 참 → 핸들러가 `write` 에서 잠듦 → 비울 메인은 핸들러 밑 → 교착 | C-5 (변형 실험: `HUNG(watchdog)`) |
| 읽기 끝 `O_NONBLOCK` | drain 루프의 마지막 `read` 가 영원히 막힘 | 감지되면 Part C SKIP |
| `FD_CLOEXEC` | `exec` 한 자식(셸 명령 등)이 pipe fd 를 물려받아 누출 | C-2 |
| 핸들러에서 errno 보존 | pipe 꽉 참(EAGAIN)일 때 메인의 errno 오염 | C-4 (변형 실험: FAIL) |
| `sa_mask` = 세 시그널 | 핸들러 중첩 (지금은 1바이트라 무해하지만 핸들러가 커지면 레이스) | C-2 |
| SIGCHLD `SA_NOCLDSTOP` | 자식이 Ctrl-Z 로 멈출 때도 깨어남 | C-2 |

실제 출력: `C-4 self-pipe capacity (1-byte non-blocking writes until EAGAIN): 65536`, `C-5 flood 100000 raises with nobody reading: OK` — 10만 번 중 65536 바이트만 남고 나머지는 EAGAIN 으로 버려졌다. 괜찮다: **읽지 않은 바이트가 하나라도 있으면 메인은 깨어난다.** self-pipe 가 전달하는 건 "일어났다" 이지 개수가 아니다 (시그널 자체가 이미 합쳐지므로 개수는 처음부터 없다).

**`loop_once`:**

```c
int r = poll(&pf, 1, timeout_ms);
if (r < 0) return errno == EINTR ? 0 : -1;    /* poll 은 SA_RESTART 여도 EINTR */
/* r > 0: read 를 EAGAIN 까지 반복 → 바이트마다 TERM/INT 면 L->stop, CHLD 면 reap_all(L) */
```

실제 출력 (C-7, C-8):

```text
C-7 3 children exited while SIGCHLD blocked -> SIGCHLD bytes=1, reaped=3
C-8 SIGALRM during poll: loop_once -> 0 then 1, SIGALRM bytes=1
```

C-7 은 OS-B01 의 SIGCHLD 회수가 왜 `if` 가 아니라 `while` 인지의 증거다. 변형 실험(`while` → `if`)은 `reaped=1` 과 `ECHILD` 체크 실패(좀비 2개 남음)로 잡힌다. C-8: 첫 바퀴의 `poll` 은 핸들러 때문에 `-1/EINTR` → 0 반환, 바이트는 pipe 에 남아 있다가 둘째 바퀴에 처리. 이게 self-pipe 의 핵심 성질 — **EINTR 이든 아니든 이벤트를 잃지 않는다.**

**`demo_daemon.c` — 감시 데몬 뼈대.** 워커 3개, 워커 1 은 300 ms 마다 실패(exit 3) → 최대 3번 재시작 (systemd `Restart=on-failure` 와 같은 생각, ELX-B13). SIGTERM 을 받으면 워커들에게 전달하고 다 회수한 뒤 종료. 실제 출력:

```text
[    0 ms] spawn worker 0 pid 29168          (worker 1, 2 도 같은 시각)
[  308 ms] worker 1 pid 29169 exited 3
[  309 ms] spawn worker 1 pid 29182
[  309 ms]   (SIGCHLD bytes this round=1, reaped=1)
   ... (614 ms, 917 ms 재시작, 1220 ms 네 번째 실패 → 재시작 한도)
[ 1415 ms] got signal 15 (Terminated: 15) -> forwarding SIGTERM to workers
[ 1416 ms] worker 2 pid 29170 killed by signal 15 (Terminated: 15)
[ 1416 ms] worker 0 pid 29168 killed by signal 15 (Terminated: 15)
[ 1416 ms]   (SIGCHLD bytes this round=1, reaped=2)
[ 1416 ms] all workers gone, restarts=3, daemon exits 0
daemon exit=0
```

마지막 라운드가 실전의 합쳐짐이다: 워커 둘이 거의 동시에 죽어 **SIGCHLD 바이트 1개에 회수 2개**. 메인 루프는 `printf` 를 마음껏 쓴다 — 핸들러가 아니니까.

<!-- code: demo_daemon.c -->

### 펌웨어로 옮기면

| POSIX 시그널 (이 레슨) | Cortex-M / RTOS |
|---|---|
| 핸들러 = 아무 명령어 사이에 끼어드는 함수 | ISR (예외 진입, 하드웨어가 레지스터 스택킹) |
| 펜딩 비트 1개 — 막힌 동안 2번 → 1번 | NVIC ISPR 비트 1개 — 같은 IRQ 펜딩은 합쳐짐 (C-M14) |
| `sigprocmask` / `sa_mask` | `__disable_irq()`/PRIMASK, BASEPRI, 같은 우선순위 비선점 (BM-M01) |
| 핸들러에서 `printf`/`malloc` 금지 | ISR 에서 newlib `printf`(재진입 미지원 빌드가 흔함)·힙·블로킹 API 금지 |
| async-signal-safe 함수만 | FreeRTOS `…FromISR()` API 만 (RTOS-M02) |
| `volatile sig_atomic_t` 플래그 | ISR 이 세우는 `volatile` 플래그 (C-B09, C-M14) |
| `sigsuspend` 로 경쟁 없는 대기 | IRQ 막고 검사 → `WFI` → 풀기 (BM-M11) |
| self-pipe (바이트가 기억됨) | ISR → 바이너리 세마포어/태스크 알림/큐 (RTOS-B08, RTOS-M01) — "준 것" 이 기억된다 |
| SIGTERM → 정리 후 종료 | 전원 차단 경고 IRQ → 플래시 로그 flush 후 정지 |
| SIGCHLD → 회수 | 워치독 태스크가 다른 태스크의 생존 신호(heartbeat) 수집 |

임베디드 리눅스 쪽에서는 이 레슨이 곧 실무다: systemd 는 정지할 때 SIGTERM 을 보내고 `TimeoutStopSec` 뒤 SIGKILL 을 보낸다 — 데몬은 SIGTERM 을 self-pipe 로 받아 센서 버퍼를 flush 하고 **시그널로** 죽어야(연습 1) 상태 보고가 정확하다 (ELX-B13). 리눅스 전용 `signalfd` 는 이 pipe 를 커널이 대신 만들어 주는 것이다 (OS-M18).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
```

```text
A-2 coalescing: raised 2x while blocked -> pending=1, handler ran 1 time(s) after unblock
... (A-3, A-4, B, C-1, C-4, C-5, C-7, C-8 줄 — 5. 풀이에 실은 그대로)
34/34 passed
```

8번 반복 모두 `34/34 passed`, 약 1.5 초 (대부분 200 ms read 3번과 300 ms 워치독). 레슨 폴더와 `/tmp` 두 곳에서 실행해 같은 결과 — 옆에 필요한 데이터 파일은 없다. 새니타이저(경고 0, 종료 코드 0):

```bash
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_asan && ./so_asan
```

틀린 구현이 정말 잡히는지 변형 실험도 했다 (solution 의 한 줄씩 바꿈): 쓰기 끝만 blocking → C-2 `nb`·C-4·C-5(`HUNG(watchdog)`) 3 FAIL, 31/34 (4 초에 끝남), 핸들러 errno 미보존 → C-4 FAIL, `while` → `if` 회수 → `reaped=1` + ECHILD FAIL, poll EINTR 을 에러로 → C-8 FAIL, 두 끝 다 blocking → SKIP. 어느 것도 멈추지 않는다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 핸들러에서 `printf` 로 디버그 출력 | 메인이 stdio/locale 락을 쥔 순간이면 교착 또는 abort (실측 둘 다) | `write(2, msg, len)` + 손으로 만든 숫자 변환 (Part B) |
| 핸들러에서 `malloc`/`free`, `exit()` | 할당기 락 재진입 — macOS 는 os_unfair_lock 재귀 감지로 abort. `exit` 는 atexit·stdio flush | 핸들러는 알리기만. 꼭 끝내야 하면 `_exit()` |
| `struct sigaction` 을 0 으로 안 채움 | 쓰레기 `sa_flags`/`sa_mask` | `memset(&sa, 0, sizeof sa); sigemptyset(&sa.sa_mask);` |
| `signal()` 사용 | 의미가 플랫폼마다 다름(재시작, 한 번 뒤 기본값 복귀) | `sigaction` + 플래그를 명시 |
| errno 를 저장·복원 안 함 | 메인의 `if (read(...) < 0 && errno == EAGAIN)` 판단이 엉뚱해짐 | `int e = errno; … errno = e;` |
| SIGCHLD 에서 `waitpid` 한 번 | 시그널이 합쳐져 자식 여럿에 1번 → 좀비 누적 | `while (waitpid(-1, &st, WNOHANG) > 0)` |
| `if (!flag) pause();` | 검사와 `pause` 사이 시그널 → 영원히 잠 (C-1 HUNG) | 막기 + `sigsuspend`, 또는 self-pipe |
| self-pipe 를 blocking 으로 | 꽉 차면 핸들러가 `write` 에서 잠들어 교착 | 두 끝 다 `O_NONBLOCK`, 핸들러는 EAGAIN 무시 |
| `poll`/`select` 의 `-1` 을 무조건 치명 에러로 | 시그널마다 루프가 죽음. `SA_RESTART` 로도 안 막힘 | `if (errno == EINTR) continue;` |
| SA_RESTART + blocking read + 플래그로 종료 | read 가 재시작돼 플래그를 영영 못 봄 | 대기 자체를 깨우는 self-pipe/`poll` 구조 |
| 핸들러에서 `volatile sig_atomic_t` 를 `++` | 비동기 핸들러에서 읽기는 C11 상 미정의, RMW 는 원자적이지도 않음 | lock-free `atomic_fetch_add` (C-M14) |
| SIGTERM 정리 후 `exit(0)` | 부모(systemd, 셸)는 "정상 종료" 로 기록 | 기본 동작 복원 후 `raise(SIGTERM)` (연습 1) |

## 8. 면접 Q&A

<details>
<summary>Q1. 시그널 핸들러에서 printf 를 부르면 왜 안 되나요? 실제로 문제가 나나요?</summary>

`printf` 는 async-signal-safe 목록(POSIX 2.4.3)에 없다. 구체적 이유: 내부에서 `FILE` 락과 로케일 락을 잡고, 버퍼를 고치고, 때로 `malloc` 한다. 시그널은 아무 명령어 사이에나 오므로 메인이 그 락을 쥐고 있을 때 핸들러가 같은 락을 잡으면 — 락의 주인은 핸들러 **밑에** 깔린 같은 스레드라 영원히 안 풀린다. 이 맥에서 실측: 100 µs 타이머로 5번 돌려 4번은 `os_unfair_lock` 재귀 감지 abort(`localeconv_l`), 1번은 `flockfile` 에서 교착(`__psynch_mutexwait`, CPU 0%). 꼬리: "재귀 락이면 괜찮지 않나?" → 교착은 피하지만 반쯤 고쳐진 버퍼를 핸들러가 덮어쓴다 (`demo_unsafe_handler mutex` 의 RECURSIVE: `"SIGlo ..."`). 더 나쁘다 — 조용하니까.

</details>

<details>
<summary>Q2. async-signal-safe 는 thread-safe 와 어떻게 다른가요?</summary>

thread-safe 는 **다른 스레드** 가 동시에 불러도 안전하다는 뜻이고 보통 락으로 이룬다. async-signal-safe 는 **같은 스레드가 자기 자신을 중단한 채** 다시 불러도 안전하다는 뜻이라 락이 오히려 독이다 (락 주인이 나 자신). 그래서 thread-safe 인 `malloc` 은 async-signal-safe 가 아니다. async-signal-safe 함수는 대부분 그냥 시스템 콜 래퍼(`write`, `kill`, `sigprocmask`)이거나 전역 상태가 없는 함수다. 재진입 가능(reentrant, C-A04)과 가까운 개념이다. ISR 에서 부를 수 있는 함수 조건과 같다.

</details>

<details>
<summary>Q3. SA_RESTART 는 무엇을 재시작하고 무엇을 안 하나요?</summary>

핸들러가 끝난 뒤 "느린 장치" 의 blocking 시스템 콜을 커널이 다시 시작한다: pipe/tty/소켓의 `read`/`write`/`recv`/`send`, `wait`/`waitpid` 등 (macOS `sigaction(2)`). 이미 일부를 옮긴 호출은 재시작하지 않고 짧은 결과를 돌려준다. 타임아웃이 있는 대기 — `poll`, `select`, `nanosleep` — 는 `SA_RESTART` 여도 `EINTR` (macOS 실측, 리눅스 `signal(7)` 도 같은 범주). 그래서 이벤트 루프는 `poll` 의 EINTR 을 항상 처리하고, `nanosleep` 은 남은 시간(`rem`)으로 다시 잔다. 꼬리: "그럼 SA_RESTART 를 항상 켜면?" → 라이브러리 호출이 EINTR 에 덜 놀라는 대신 blocking read 에 묶인 메인이 종료 시그널을 못 본다. 종료는 대기를 깨우는 경로(self-pipe)로.

</details>

<details>
<summary>Q4. self-pipe trick 을 설명하고, 왜 pipe 를 non-blocking 으로 만드나요?</summary>

핸들러는 pipe 쓰기 끝에 1바이트만 쓰고(errno 보존), 메인 루프는 읽기 끝을 다른 fd 들과 함께 `poll` 한다. 시그널이 "읽을 수 있는 fd" 로 바뀌므로 ① 이벤트 루프 하나로 통일되고 ② 바이트가 커널 버퍼에 **기억** 되니 검사↔잠들기 경쟁이 없고 ③ 실제 처리는 일반 코드에서 하니 `printf`/`malloc` 을 마음껏 쓴다. non-blocking 인 이유: 쓰기 끝 — 메인이 바쁜 동안 pipe(이 맥에서 64 KB)가 차면 핸들러가 `write` 에서 잠드는데 pipe 를 비울 메인은 핸들러 밑에 있어 교착. 꽉 찼으면 EAGAIN 으로 버려도 된다 — 이미 안 읽은 바이트가 있으니 메인은 깨어난다. 읽기 끝 — "EAGAIN 까지 drain" 이 마지막 read 에서 멈추지 않게. `FD_CLOEXEC` 로 exec 하는 자식에게 새지 않게. 꼬리: "대안은?" → 리눅스 `signalfd`(OS-M18), BSD/macOS `kqueue` `EVFILT_SIGNAL`(OS-A11), 멀티스레드면 전용 스레드의 `sigwait`(연습 4).

</details>

<details>
<summary>Q5. SIGCHLD 핸들링에서 흔한 버그는? 좀비는 어떻게 생기나요?</summary>

좀비는 종료했지만 부모가 아직 `wait` 하지 않은 자식이다 (종료 코드를 보관하려고 프로세스 테이블 항목이 남는다, OS-B01). 버그 ① SIGCHLD 한 번에 `waitpid` 한 번 — 표준 시그널은 쌓이지 않아 자식 여럿이 시그널 하나로 합쳐진다 (C-7: 자식 3개 → 바이트 1개). `while (waitpid(-1, &st, WNOHANG) > 0)` 로 고친다. ② 핸들러 안에서 `waitpid` 후 `printf`/자료구조 갱신 — Q1 문제. 회수는 루프에서. ③ `WNOHANG` 없이 핸들러에서 `wait` — 아직 살아 있는 자식 때문에 핸들러가 막힘. ④ 라이브러리가 `system()`/`popen()` 으로 만든 자식까지 `waitpid(-1)` 이 가로채는 문제 — 큰 프로그램에서는 pid 별 `waitpid` 나 리눅스 `pidfd`(OS-M18). 꼬리: "자식을 신경 쓰기 싫으면?" → `SIGCHLD` 를 `SIG_IGN` 또는 `SA_NOCLDWAIT` → 좀비가 안 남는다 (macOS 실측: `waitpid → ECHILD`). 대신 종료 코드도 못 받는다.

</details>

<details>
<summary>Q6. volatile sig_atomic_t 는 정확히 무엇을 보장하나요? 플래그만으로 충분한가요?</summary>

`sig_atomic_t` 는 시그널이 끼어들어도 한 번의 읽기/쓰기가 찢어지지 않는 정수 타입(이 맥에서 4바이트 `int`), `volatile` 은 컴파일러가 메인 루프의 읽기를 루프 밖으로 빼지 못하게 한다. C11 7.14.1.1p5 가 비동기 핸들러에 허용하는 정적 객체 접근은 이 타입에 **대입** 과 lock-free 원자 객체뿐이다 (자세히 C-M14). 플래그만으로는 부족한 경우: ① 개수(합쳐짐 + `++` 은 RMW) ② **대기와의 결합** — `if (!flag) pause();` 는 경쟁 (C-1 HUNG). 플래그를 쓰려면 막고-검사-`sigsuspend` 패턴, 아니면 self-pipe. 꼬리: "멀티스레드면?" → `volatile` 은 스레드 간 동기화가 아니다. 스레드 사이 전달은 `atomic` + 메모리 순서 (C-A02, OS-M08).

</details>

<details>
<summary>Q7. 멀티스레드 프로그램에서는 시그널을 어떻게 다루나요?</summary>

프로세스로 보낸 시그널(`kill`)은 그 시그널을 막지 않은 **아무 스레드** 에게나 전달된다. 표준 패턴: `main` 이 스레드를 만들기 **전에** `pthread_sigmask` 로 SIGINT/SIGTERM/SIGHUP 등을 막는다 (마스크는 상속) → 전용 시그널 스레드 하나가 `sigwait()` 로 동기적으로 받아 일반 코드로 처리한다 (연습 4: 핸들러가 아예 안 돈다, 막힌 동안 USR1 ×2 → 1번). macOS 에는 `sigtimedwait`/`sigwaitinfo` 가 없어서 `sigwait` 만 쓸 수 있다(헤더 확인). 리눅스는 `signalfd` 로 epoll 에 넣는다 (OS-M18). SIGSEGV 같은 동기 시그널은 일으킨 스레드에 가므로 막지 않는다. 꼬리: "fork 후 자식의 마스크는?" → 부모 스레드의 마스크를 상속하고, `exec` 해도 마스크와 무시(SIG_IGN) 설정은 남는다 — 그래서 자식 쪽에서 원래대로 돌려 놓아야 한다 (하네스의 `reset_signals_for_child`). 핸들러로 잡던 시그널은 `exec` 때 기본 동작으로 돌아간다 (macOS `sigaction(2)`).

</details>

<details>
<summary>Q8. SIGTERM 을 받고 정리한 뒤 exit(0) 하면 무엇이 문제인가요? SIGKILL 은?</summary>

부모(셸, systemd, 감시 데몬)는 `waitpid` 의 status 로 "정상 종료(WIFEXITED, 0)" 를 본다 — 시그널에 죽었다는 정보가 사라진다. 셸 관례는 128+sig (OS-B01). 올바른 관용구: 정리 → 기본 동작 복원 → 막혀 있으면 풀기 → `raise(SIGTERM)` (연습 1 실측: `exit(0)` 은 `exited 0`, `die_by_signal` 은 `signaled 15`). SIGKILL 과 SIGSTOP 은 잡거나 막거나 무시할 수 없다 (`sa_mask` 에 넣어도 조용히 무시된다 — macOS 맨 페이지). 그래서 정리 기회는 SIGTERM 뿐이고, systemd 도 SIGTERM → `TimeoutStopSec` 후 SIGKILL 순서다. 정리 시간이 긴 데몬(플래시 flush)은 그 타임아웃 안에 끝내야 한다.

</details>

## 9. 추가 연습

### 연습 1 — 정리한 뒤 "시그널로" 죽기

SIGTERM 핸들러는 플래그만 세우고, 메인이 정리(파이프에 "cleanup" 쓰기)한 뒤 `die_by_signal(SIGTERM)` 으로 끝내라. 부모가 `WIFSIGNALED && WTERMSIG == SIGTERM` 을 봐야 한다. `exit(0)` 버전과 비교.

<details>
<summary>답 보기</summary>

① `sigaction(sig, SIG_DFL)` 로 내 핸들러 제거 → ② `sigprocmask(SIG_UNBLOCK, {sig})` (막혀 있으면 raise 가 펜딩만 된다) → ③ `raise(sig)` = 기본 동작으로 종료 → 그래도 돌아오면(기본 동작이 무시인 시그널) `_exit(128 + sig)`.

실제 출력: `ex1 exit(0) : cleanup=yes status: exited 0` vs `ex1 die_by_signal : cleanup=yes status: signaled 15`. 둘 다 정리는 했지만 부모가 보는 사실이 다르다. bash 도 `trap ... TERM` 안에서 같은 관용구(`trap - TERM; kill -TERM $$`)를 쓴다.

</details>

### 연습 2 — 두 번째 Ctrl-C 는 즉시 종료

첫 SIGINT 는 우아한 종료를 시작(플래그), 정리가 오래 걸려 사용자가 한 번 더 누르면 바로 죽어야 한다. `install_once(sig, fn)` 을 `SA_RESETHAND` 로 구현하라.

<details>
<summary>답 보기</summary>

`sa.sa_flags = SA_RESETHAND | SA_RESTART;` — 커널이 핸들러를 부르는 순간 처리 방식을 `SIG_DFL` 로 되돌린다. 그래서 두 번째 SIGINT 는 기본 동작(종료). 자식에서 확인: 첫 `raise` 후 `g_int == 1` 이고 `sigaction` 조회 결과 `SIG_DFL`, 두 번째 `raise` 에서 `killed by 2`. 실제 출력: `ex2 first SIGINT -> flag set, reset to SIG_DFL, second SIGINT -> killed by 2`. (macOS 맨 페이지: SIGILL/SIGTRAP 에는 `SA_RESETHAND` 가 무시된다.)

</details>

### 연습 3 — write_all: EINTR 과 짧은 쓰기

`ssize_t write_all(int fd, const void *buf, size_t n)` — 시그널(0.5 ms 마다 SIGALRM, `SA_RESTART` 없음)이 쏟아지는 동안 느린 독자에게 1 MiB 를 pipe 로 보내 독자가 체크섬까지 그대로 받게 하라.

<details>
<summary>답 보기</summary>

`while (left > 0) { k = write(fd, p, left); if (k < 0) { if (errno == EINTR) continue; return -1; } p += k; left -= k; }` — 핵심: 시그널이 오기 전에 **조금이라도** 옮겼으면 `write` 는 EINTR 이 아니라 옮긴 바이트 수를 돌려준다(짧은 쓰기). 실측: 맨 `write(1 MiB)` 한 번이 `196608` (192 KiB, ASan 빌드에서는 65536) 만 쓰고 돌아왔다. 남은 건 `write_all` 이 끝냈고 독자의 FNV-1a 체크섬 일치. 독자가 죽으면 SIGPIPE 로 쓰는 쪽이 죽으므로 `SIGPIPE` 를 무시하고 `EPIPE` 로 받는다.

</details>

### 연습 4 — 핸들러 없이 받기: sigwait

SIGUSR1, SIGTERM 을 막아 두고 자신에게 USR1 ×2, TERM ×1 을 보낸 뒤 `next_signal(&set)` (`sigwait`) 로 하나씩 꺼내라. 몇 개가 나오나? 핸들러는 도나?

<details>
<summary>답 보기</summary>

`sigwait(set, &sig)` 는 set 중 펜딩된 것 하나를 **꺼내서**(펜딩 해제) 번호를 준다. 에러는 errno 가 아니라 반환값. 실제 출력: `ex4 sigwait got 15 then 30 (USR1=30 TERM=15), pending after: none, handler ran: 0` — USR1 두 번은 하나로 합쳐졌고, 꺼낸 시그널은 핸들러를 타지 않았다. 어떤 순서로 나오는지는 POSIX 가 정하지 않으므로 테스트는 집합으로만 본다. 멀티스레드 데몬의 표준 구조(Q7)이고, 핸들러 제약이 아예 없다 — 받은 뒤엔 일반 코드다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice    # 12/12 passed, 0.1 초 미만
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_asan && ./pr_asan   # 12/12, 경고 0
```

## 10. 핵심 정리

- 시그널 = 사용자 공간의 인터럽트. 펜딩 **비트 1개** 라 같은 시그널은 합쳐진다 (막힌 동안 2번 → 1번, 자식 3개 → SIGCHLD 1번) → 개수는 시그널로 못 센다, SIGCHLD 는 `waitpid(-1, WNOHANG)` **루프**.
- 설치는 `sigaction`: 0 으로 채우고 `sa_handler`·`sa_mask`(중첩 방지)·`sa_flags`(`SA_RESTART`, `SA_NOCLDSTOP`, `SA_RESETHAND`). `signal()` 은 쓰지 않는다.
- `SA_RESTART` 는 read/write/waitpid 를 재시작하지만 `poll`/`select`/`nanosleep` 은 여전히 EINTR (macOS 실측). 이벤트 루프는 EINTR 을 "다시" 로 처리.
- 핸들러에서는 async-signal-safe 함수만 (POSIX 2.4.3): `write`, `_exit`, `kill`, `sigprocmask` … `printf`/`malloc`/락 금지. 이유는 **중단된 코드가 쥔 락** — 실측: 교착(flockfile), abort(os_unfair_lock 재귀), 재귀 락이면 조용한 손상.
- 핸들러는 들어올 때 `errno` 저장, 나갈 때 복원. 로그는 스택 버퍼 + `write` 한 번.
- 플래그 + `pause()` 는 경쟁 → 막고 검사 → `sigsuspend(old)` (= IRQ 막고 검사 → WFI).
- **self-pipe**: 두 끝 `O_NONBLOCK|FD_CLOEXEC`, 핸들러는 1바이트(EAGAIN 무시), 메인은 `poll` → drain → dispatch. 바이트가 기억되므로 경쟁·EINTR 에 강하다. Linux `signalfd`(OS-M18), macOS `kqueue`(OS-A11), 멀티스레드는 `sigwait`.
- SIGTERM → 정리 후 기본 동작으로 `raise` 해서 죽어야 부모가 정확히 안다. 펌웨어도 같다: ISR 은 알리기만, 일은 태스크에서.

## 참고자료

- POSIX.1-2017 *2.4 Signal Concepts* — 2.4.1 생성·전달·펜딩, 2.4.3 Signal Actions (async-signal-safe 함수 표, 핸들러에서의 정적 객체 접근 제한): <https://pubs.opengroup.org/onlinepubs/9699919799/functions/V2_chap02.html#tag_15_04>
- POSIX `sigaction`: <https://pubs.opengroup.org/onlinepubs/9699919799/functions/sigaction.html> · `sigsuspend`: <https://pubs.opengroup.org/onlinepubs/9699919799/functions/sigsuspend.html> · `sigwait`: <https://pubs.opengroup.org/onlinepubs/9699919799/functions/sigwait.html> · `write` (PIPE_BUF 원자성): <https://pubs.opengroup.org/onlinepubs/9699919799/functions/write.html> · `kill`: <https://pubs.opengroup.org/onlinepubs/9699919799/functions/kill.html>
- macOS 맨 페이지 (이 맥에서 확인): `man 2 sigaction` (SA_RESTART 대상 호출, safe 함수 목록, errno 복원 권고, SIGKILL/SIGSTOP 은 sa_mask 에서 무시), `man 3 signal` (`signal()` 은 SA_RESTART 로 설치), `man 2 sigprocmask`, `man 2 poll`
- Linux `signal(7)` — "Interruption of system calls and library functions by signal handlers" (재시작되는 호출 / 절대 재시작 안 되는 호출 목록), `signal-safety(7)`: <https://man7.org/linux/man-pages/man7/signal.7.html>, <https://man7.org/linux/man-pages/man7/signal-safety.7.html>
- C11 (N1570) 7.14.1.1p5 (비동기 핸들러 제약), 5.1.2.3p5, 7.14p2 `sig_atomic_t`: <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- OSTEP 5장 *Process API* 5.5절 (kill 과 시그널 언급), 6장 *Limited Direct Execution* (trap·인터럽트 — 시그널의 커널 쪽 짝), 33장 *Event-based Concurrency* (select/poll 이벤트 루프, Aside: UNIX Signals — 그 예제는 핸들러에서 `printf` 를 부르는데, 설명용으로는 괜찮지만 이 레슨 기준으로는 async-signal-safe 가 아니다). 로컬: `operating-system-three-easy-pieces/book-md/C05_interlude_process_api.md`, `C33_event_based_concurrency_advanced.md`
- W. R. Stevens, S. Rago, *Advanced Programming in the UNIX Environment* 3판, 10장 Signals (OSTEP 이 권하는 책)
- D. J. Bernstein, "The self-pipe trick": <https://cr.yp.to/docs/selfpipe.html>
- 관련 레슨: OS-B01 (fork/exec/wait, 좀비, 128+sig), OS-B02 (pipe·fd), OS-B06 (lost wakeup), OS-M18 (signalfd·eventfd·pidfd), OS-A11 (kqueue 데몬, SIGTERM/SIGHUP), C-M14 (ISR 공유 데이터, sig_atomic_t, 펜딩 합쳐짐), C-B09 (volatile), C-A04 (재진입성), RTOS-B08 (ISR → 태스크 세마포어), RTOS-M01 (태스크 알림), RTOS-M02 (FromISR 지연 처리), BM-M01 (NVIC·ISR 규칙), BM-M11 (WFI 슬립 진입 레이스), ELX-B13 (systemd 정지·재시작)
