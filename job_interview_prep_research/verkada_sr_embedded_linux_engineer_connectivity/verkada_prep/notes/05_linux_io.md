# 🐧 임베디드 Linux I/O & 이벤트 루프 (Embedded Linux I/O & Event Loops) — Q39~48

> 직함이 **Embedded Linux Engineer** 다. GC31-E/GW31-E 게이트웨이의 연결 데몬은
> 결국 "모뎀 제어 채널 + 클라우드 소켓 + 재연결 타이머 + 종료 시그널"을 한 루프에서
> 돌리는 프로그램이다. 여기가 Don 의 가장 약한 영역이므로 이 노트는 **드릴보다 교육**
> 비중이 높다. 코드는 macOS 에서 돌도록 POSIX 로 쓰되, 면접에서 말해야 하는
> **Linux 전용 API(epoll/timerfd/signalfd/systemd)는 아래 이론 절**에서 확실히 잡는다.

---

## 1. 핵심 아이디어 — 게이트웨이 데몬 = 하나의 이벤트 루프

싱글 스레드 + 논블로킹 I/O + 하나의 `poll()`. 스레드를 늘리는 대신 **fd 를 늘린다.**
락이 없으니 데드락도 없고, 상태가 한 스레드에 모여 있어 테스트하기 쉽다.

```
                         ┌──────────────── gateway daemon (single thread) ──────────────┐
  modem /dev/ttyUSB2 ──▶ │ fd0  line-framing reader ──▶ AT 응답 파서                     │
  cloud TCP socket   ──▶ │ fd1  frame reader        ──▶ Command 프로토콜                 │
  self-pipe / eventfd──▶ │ fd2  wakeup (cancel, config-changed, 다른 스레드의 요청)       │
  timerfd (Linux)    ──▶ │ fd3  주기 작업 (없으면 poll timeout 으로 대체)                 │
                         │                                                              │
                         │   for(;;) {                                                  │
                         │     to = poll_timeout_from(next_deadline, WATCHDOG_CAP);     │
                         │     n  = poll(fds, nfds, to);       ← 유일한 블로킹 지점       │
                         │     if (n < 0 && errno == EINTR) continue;   // 시그널        │
                         │     if (g_stop) break;              // 종료 플래그            │
                         │     if (n == 0) { on_timer(); kick_watchdog(); continue; }   │
                         │     for each fd with revents: drain until EAGAIN;            │
                         │   }                                                          │
                         └──────────────────────────────────────────────────────────────┘
                                          │
                              backoff_delay_ms(attempt++) 로 재연결
```

**한 문장 요약**: 블로킹은 `poll()` 한 곳에서만 일어나고, 나머지는 전부 논블로킹이며,
"깨우고 싶으면" self-pipe 에 1바이트를 쓴다.

---

## 2. 규칙 ① — 부분 I/O 와 EINTR (Q39, Q40)

`read()`/`write()` 는 **요청한 만큼을 약속하지 않는다.**

| 상황 | `read` 리턴 | 의미 | 해야 할 일 |
|---|---|---|---|
| 데이터 일부 도착 | `0 < k < n` | short read — **정상** | 남은 만큼 다시 읽거나, 누적 버퍼에 쌓기 |
| 상대가 close | `0` | EOF | fd 를 poll 집합에서 제거, 재연결 |
| 논블로킹, 데이터 없음 | `-1`, `EAGAIN`/`EWOULDBLOCK` | **정상** | 루프로 복귀 (에러 로그 금지) |
| 시그널이 왔다 | `-1`, `EINTR` | 중단됨 | **재시도** |
| 그 외 | `-1` | 진짜 에러 | 정리 후 보고 |

```c
ssize_t read_full(int fd, void *buf, size_t n) {   // 블로킹 fd 용
    unsigned char *p = buf; size_t got = 0;
    while (got < n) {
        ssize_t k = read(fd, p + got, n - got);
        if (k < 0) { if (errno == EINTR) continue; return -1; }
        if (k == 0) break;            // EOF → 짧게 리턴 (에러 아님!)
        got += (size_t)k;
    }
    return (ssize_t)got;
}
```

- **`write` 의 부분 쓰기가 더 위험하다.** 소켓/파이프 버퍼가 차면 `write` 는 쓸 수 있는
  만큼만 쓰고 돌아온다. 루프를 안 돌면 프레임이 반쪽만 나가고, 상대는 영원히 다음
  프레임을 기다린다 → "가끔 클라우드 연결이 멈춘다" 급 버그.
- `-Wunused-result`: `write()` 리턴값 무시는 컴파일러가 경고한다. 이유가 있다.
- **논블로킹 fd 에 `read_full` 을 쓰면 안 된다.** EAGAIN 에서 -1 이 되거나, 재시도하면
  바쁜 대기가 된다. 논블로킹에는 "EAGAIN 까지 읽고 루프로 복귀"(`read_nb`)가 맞다.

### O_NONBLOCK 설정의 함정

```c
fcntl(fd, F_SETFL, O_NONBLOCK);              // ❌ 나머지 플래그를 전부 날림
int fl = fcntl(fd, F_GETFL, 0);
fcntl(fd, F_SETFL, fl | O_NONBLOCK);         // ✅ read-modify-write
```
`O_NONBLOCK`(파일 상태 플래그, `F_GETFL`)과 `FD_CLOEXEC`(fd 플래그, `F_GETFD`)는
**다른 네임스페이스**다. 헷갈리면 바로 티가 난다.

> `EAGAIN` 과 `EWOULDBLOCK` 은 Linux/macOS 에서 같은 값이지만 표준은 같다고 보장하지
> 않는다. `if (errno == EAGAIN || errno == EWOULDBLOCK)` 이 이식성 있는 관용구다.

---

## 3. 규칙 ② — poll vs epoll vs kqueue vs select (Q41)

| | `select` | `poll` | `epoll` (Linux) | `kqueue` (BSD/macOS) |
|---|---|---|---|---|
| fd 개수 한계 | `FD_SETSIZE`(보통 1024) | 없음 | 없음 | 없음 |
| 호출당 비용 | O(n) 복사 + O(n) 스캔 | O(n) 복사 + O(n) 스캔 | **O(활성 fd)** — 관심 집합이 커널에 상주 | O(활성 fd) |
| 관심 집합 | 매번 재구축(호출이 파괴) | 매번 전달 | `epoll_ctl` 로 1회 등록 | `kevent` changelist |
| 트리거 모드 | level | level | **level / edge(EPOLLET)** | level / edge |
| 타임아웃 해상도 | µs (`timeval`) | **ms (int)** | ms (`epoll_wait`) / ns (`epoll_pwait2`) | ns (`timespec`) |
| 이식성 | 어디나 | **POSIX — 어디나** | Linux 전용 | BSD/macOS 전용 |
| 적합한 규모 | 레거시 | fd 수십 개 | fd 수천~수만 | 수천~수만 |

**게이트웨이 데몬의 정답은 보통 `poll`** 이다 — fd 가 5~20개라 epoll 의 이점이 없고,
코드가 짧고 이식성이 있다. epoll 은 fd 수백 개 이상, 즉 서버 쪽 이야기다.
이 트레이드오프를 말할 수 있는 것 자체가 평가 항목("설계 결정의 정당화")이다.

### epoll 을 쓴다면 반드시 아는 것

```c
int ep = epoll_create1(EPOLL_CLOEXEC);
struct epoll_event ev = { .events = EPOLLIN, .data.fd = sock };
epoll_ctl(ep, EPOLL_CTL_ADD, sock, &ev);
int n = epoll_wait(ep, evs, MAXEV, timeout_ms);
```
- **Level-triggered(기본)**: 데이터가 남아 있으면 계속 보고. 한 번에 다 안 읽어도 안전.
- **Edge-triggered(`EPOLLET`)**: 상태가 "변할 때만" 보고 → **반드시 논블로킹 + EAGAIN 이
  날 때까지 전부 읽어야** 한다. 안 그러면 남은 데이터가 영원히 잠긴다. ET 의 이점은
  동시 접속이 아주 많을 때의 wakeup 감소뿐 — 임베디드 데몬에서 ET 는 보통 과설계다.
- `EPOLLONESHOT` 은 멀티스레드에서 한 fd 를 한 스레드만 처리하게 할 때.

### poll 루프의 3대 버그

1. **EOF fd 를 안 뺀다** → `POLLHUP` 이 매번 즉시 리턴 → CPU 100% 스핀.
2. **`revents` 를 `events` 로만 검사한다** → `POLLHUP/POLLERR/POLLNVAL` 은 요청 안 해도
   올라온다. 이 비트에서도 `read()` 를 해봐야 EOF/에러가 구분된다.
3. **타임아웃을 재계산 안 한다** → EINTR 로 깰 때마다 타임아웃이 리셋되어 마감이 무한정
   밀린다. 그래서 `deadline` 을 잡고 매 회전 `ms_remaining()` 을 다시 구한다.

---

## 4. 규칙 ③ — 루프를 깨우는 법 (Q42, Q47)

이벤트 루프의 고전 문제: **poll 안에서 자고 있는데 "지금 종료해" 를 어떻게 전하나?**
전역 플래그만 세우면 poll 이 타임아웃될 때까지 모른다.

**self-pipe trick**: 파이프를 만들어 읽기단을 항상 poll 집합에 넣어두고, 깨우려는
쪽(다른 스레드 / 시그널 핸들러)이 1바이트를 쓴다.

```c
// 시그널 핸들러에서 — write 는 async-signal-safe 하다
static void on_sigterm(int s) {
    (void)s;
    int save = errno;              // 핸들러는 errno 를 복원해야 한다
    g_stop = 1;                    // volatile sig_atomic_t
    write(g_sp.wr, "x", 1);        // 루프를 즉시 깨운다
    errno = save;
}
```
- 양쪽 끝 **O_NONBLOCK**: 파이프가 이미 차 있으면 `EAGAIN` → "이미 깨울 예정"이므로 성공.
- 깬 뒤 **반드시 drain**(EAGAIN 까지 읽기). 안 비우면 level-triggered poll 이 계속 즉시
  리턴해서 busy loop.
- **FD_CLOEXEC**: 데몬이 `fork/exec`(pppd, wpa_supplicant 호출 등) 할 때 fd 누수 방지.

| 방법 | 플랫폼 | 비고 |
|---|---|---|
| **self-pipe** | 어디나 | fd 2개. 가장 이식성 있음 |
| `eventfd(0, EFD_NONBLOCK\|EFD_CLOEXEC)` | Linux | fd **1개**, 8바이트 카운터. self-pipe 의 현대판 |
| `signalfd` | Linux | 시그널을 **fd 로** 받아 핸들러 자체를 없앤다. 반드시 `pthread_sigmask` 로 먼저 블록 |
| `pselect`/`ppoll` | POSIX/Linux | 시그널 마스크를 원자적으로 교체 → self-pipe 없이 레이스 회피 |

### 시그널 핸들러에서 허용되는 것 (Q47)

- **async-signal-safe 함수만**: `write`, `_exit`, `sigaction`, `kill`, `read` …
  **금지**: `printf`, `malloc/free`, `pthread_mutex_lock` — 핸들러가 malloc 중간에
  끼어들면 힙 락이 재진입되어 그대로 데드락이다.
- **쓸 수 있는 전역은 `volatile sig_atomic_t` 뿐** (또는 C11 `atomic_int` lock-free).
  `volatile` 은 "최적화로 읽기를 생략하지 말라"일 뿐 원자성도 순서도 보장하지 않는다 —
  그래서 **플래그 하나**에만 쓴다.
- `signal()` 말고 **`sigaction()`**: 시맨틱이 이식성 있고 마스크를 지정할 수 있다.
  `SA_RESTART` 를 **켜지 않아야** `poll` 이 `EINTR` 로 즉시 깨어나 플래그를 본다.
  (켜면 일부 시스템콜이 자동 재시작되어 종료가 지연된다.)
- 멀티스레드에서는 시그널이 **아무 스레드에나** 배달된다. 관례: 메인이 아닌 모든 스레드에서
  `pthread_sigmask(SIG_BLOCK, ...)` 로 막고 전담 스레드 하나가 받는다.

---

## 5. 타이밍 — monotonic, timerfd, watchdog (Q45)

```c
struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);   // ✅
time(NULL); gettimeofday(...);                             // ❌ 타임아웃에 쓰지 말 것
```
`CLOCK_REALTIME` 은 NTP/사용자가 되돌릴 수 있다. 게이트웨이가 부팅 직후 NTP 동기화로
시계를 몇 시간 점프시키면 realtime 기반 타임아웃은 그대로 멈춘 것처럼 보인다.
(참고: Linux `pthread_cond_timedwait` 는 기본이 REALTIME 이라
`pthread_condattr_setclock(&a, CLOCK_MONOTONIC)` 이 필요하다. macOS 는 그 API 가 없고
`pthread_cond_timedwait_relative_np` 를 쓴다 — 세트 02 참고.)

| Linux 타이머 방식 | 특징 | 언제 |
|---|---|---|
| `poll` timeout | fd 불필요, ms 해상도, **가장 단순** | 타이머가 1~2개 |
| `timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)` | 만료가 **fd 의 읽기 이벤트**가 됨 → 루프에 자연스럽게 합류, 놓친 만료 횟수를 read 로 알려줌 | 타이머 여러 개 / 주기 정확도 필요 |
| `timer_create` + 시그널 | 핸들러 제약이 큼 | 비권장 |
| 정렬된 deadline 힙 + poll timeout | fd 0개로 타이머 N개 | 타이머 많고 이식성 필요 |

**systemd watchdog** (게이트웨이 데몬은 거의 항상 systemd 유닛이다):
```ini
[Service]
Type=notify
WatchdogSec=30
Restart=always
```
```c
sd_notify(0, "READY=1");           // 기동 완료 보고 (Type=notify)
sd_notify(0, "WATCHDOG=1");        // 루프가 살아 있다는 신호, WatchdogSec/2 마다
```
그래서 `poll` 타임아웃에 **상한(cap)** 을 둔다 — 이벤트가 없어도 주기적으로 깨어나
watchdog 을 쳐야 하기 때문. `poll_timeout_from(deadline, 10000)` 의 cap 이 그 역할이다.
하드웨어 watchdog 이면 `/dev/watchdog` 에 주기적으로 쓴다. GW31-E 의
"오프라인 30분이면 자가 재부팅"도 같은 계열의 안전장치다.

---

## 6. 전원 손실 안전 (Q46) — write + fsync + rename

야외 장비는 전원이 예고 없이 끊긴다(PoE 재협상, 태양광 배터리 방전, -25°C 히터 부하).
설정 파일을 제자리에서 덮어쓰면 **반쪽 파일**로 부팅해 벽돌이 된다.

```
① mkstemp("<path>.tmpXXXXXX")   ← 반드시 같은 디렉토리(= 같은 파일시스템, rename 은 EXDEV 로 실패)
② write_full(fd, data, n)
③ fsync(fd)                     ← 데이터가 실제 미디어에 도달
④ close(fd)  (+ chmod 0644, mkstemp 는 0600)
⑤ rename(tmp, path)             ← POSIX 원자성: 독자는 옛 파일 아니면 새 파일만 본다
⑥ fsync(디렉토리 fd)            ← 빼먹기 쉬움! 없으면 rename 자체가 전원 손실로 사라질 수 있다
실패 시: unlink(tmp)            ← 쓰레기 누적 금지
```
`fsync` 없이 `rename` 만 하면 "파일 이름은 새 것, 내용은 0바이트"라는 최악의 상태가 실제로
나온다(ext4 의 지연 할당). 임베디드에서는 여기에 **A/B 슬롯 + 부팅 카운터**(U-Boot
`bootcount`/`upgrade_available`)를 더해 OTA 롤백까지 만든다 — follow-up 으로 말할 거리.

---

## 7. Linux 살펴보기 — /proc, /sys, netlink (면접 대화용)

코드로는 안 다루지만 **Embedded Linux Engineer 면접에서 반드시 나온다.**

| 무엇 | 어디서 | 쓰임 |
|---|---|---|
| fd 누수 확인 | `ls /proc/<pid>/fd` 개수 세기, `lsof -p` | "며칠 뒤 EMFILE" 디버깅의 1단계 |
| 메모리 | `/proc/<pid>/status` (VmRSS), `smaps_rollup` | 누수/워터마크 |
| 네트워크 | `/proc/net/dev`, `ss -tanp`, `/sys/class/net/<if>/operstate` | 링크 up/down, WAN failover |
| 모뎀 | `/dev/ttyUSB*`(AT), `/dev/cdc-wdm*`(QMI/MBIM), ModemManager D-Bus | LTE Cat12, 듀얼 SIM 전환 |
| GPIO/I2C/전원 | `/sys/class/gpio`(구), **libgpiod `/dev/gpiochip*`(현재 표준)**, `/dev/i2c-*` | PoE 제어, 히터, 센서 |
| 링크 상태 알림 | **netlink** (`AF_NETLINK`, `RTMGRP_LINK`) — 소켓이므로 **poll 집합에 그대로 합류** | 폴링 없이 캐리어 변화 감지 |
| 파일 변경 알림 | `inotify` (fd 기반) | 설정 리로드 |
| 커널 이벤트 | `udev` / `AF_NETLINK NETLINK_KOBJECT_UEVENT` | USB 모뎀 재열거 |

> 포인트: **Linux 의 "모든 게 fd" 철학** — 타이머도(timerfd), 시그널도(signalfd),
> 링크 상태도(netlink), 파일 변경도(inotify) fd 가 되어 같은 `poll/epoll` 루프에
> 합류한다. 이 한 문장을 말할 수 있으면 "Linux 를 이해한다"는 신호가 된다.

---

## 8. 재연결 정책 (Q44)

200만 대가 클라우드 장애 복구 순간 동시에 재접속하면 **thundering herd** 로 복구를 방해한다.

```c
delay = min(base * 2^attempt, cap);
delay = delay ± (delay * jitter_pct / 100);     // 지터로 흩뿌린다
```
- `base=500ms, cap=30s, jitter=20%` 가 실무 기본값. 성공하면 `attempt=0` 으로 리셋.
- **난수를 함수 안에서 뽑지 말고 인자로 주입**한다 → 순수 함수 → 하드웨어·시간 없이
  단위 테스트 가능. 이게 리크루터 메일의 "modular, testable embedded software" 에 대한
  가장 직접적인 대답이다(같은 이유로 `now_ms` 도 주입 가능하게 만드는 게 정석).
- **오버플로 주의**: `1 << attempt` 를 그냥 하면 attempt 가 32 를 넘을 때 UB.
  cap 에서 먼저 끊거나 64비트로 계산한다.

---

## 9. 흔한 함정 (인터뷰 감점 포인트)

1. `read()` 가 요청한 만큼 읽었다고 가정 — **가장 흔한 감점**.
2. `EINTR` 을 에러로 처리 → 게이트웨이가 시그널 한 번에 연결을 끊는다.
3. `EAGAIN` 을 에러 로그로 남김 → 로그가 홍수가 되고 진짜 에러가 묻힌다.
4. `fcntl(fd, F_SETFL, O_NONBLOCK)` 로 기존 플래그를 날림.
5. EOF 난 fd 를 poll 집합에 남겨둠 → **CPU 100%**.
6. poll 타임아웃을 매 회전 재계산하지 않음 → 마감이 무한정 밀림.
7. 시그널 핸들러에서 `printf`/`malloc` 호출 → 재진입 데드락.
8. 종료 플래그를 그냥 `int` 로 선언 → `volatile sig_atomic_t` 여야 한다.
9. self-pipe 를 drain 하지 않음 → busy loop.
10. 설정 파일 제자리 덮어쓰기 / `fsync` 생략 → 전원 손실에 벽돌.
11. `close()` 가 EINTR 이라고 재시도 → **다른 스레드가 방금 연 같은 번호의 fd 를 닫는다**
    (Linux 에서 close 는 EINTR 여도 fd 를 이미 반납했다).
12. 에러 경로마다 `return` 을 흩뿌려 fd 하나를 빠뜨림 → 며칠 뒤 `EMFILE`.
13. 타임아웃에 `CLOCK_REALTIME` 사용 → NTP 점프에 멈춤.
14. edge-triggered epoll 을 쓰면서 EAGAIN 까지 안 읽음 → 데이터가 영원히 잠김.

---

## 10. 면접에서 말할 것 (한국어 + 영어)

**구조를 먼저 선언**
- "게이트웨이 데몬은 단일 스레드 이벤트 루프로 잡겠습니다. 모뎀 채널, 클라우드 소켓,
  깨우기용 self-pipe 를 전부 논블로킹 fd 로 만들고 `poll()` 한 곳에서만 블록합니다.
  락이 없으니 데드락이 없고 상태가 한 곳에 모여 테스트하기 쉽습니다."
  > *"I'd model the gateway daemon as a single-threaded event loop: the modem channel,
  > the cloud socket, and a self-pipe are all non-blocking fds, and `poll()` is the only
  > place that blocks. No locks means no deadlocks, and all state lives in one place,
  > which makes it testable."*

**poll vs epoll 트레이드오프**
- "fd 가 스무 개 남짓이라 `poll` 로 충분하고 이식성도 얻습니다. epoll 은 관심 집합이
  커널에 상주해 fd 수천 개부터 이득이 납니다. edge-triggered 는 EAGAIN 까지 전부 읽어야
  해서 실수 여지가 크고, 이 규모에서는 과설계입니다."
  > *"With ~20 fds, `poll` is enough and stays portable. `epoll` wins once you have
  > thousands of fds because the interest set lives in the kernel. Edge-triggered mode
  > requires draining to EAGAIN and is over-engineering at this scale."*

**부분 I/O · EINTR**
- "`read`/`write` 는 부분 전송이 정상입니다. 짧은 read 는 에러가 아니라 EOF 신호이고,
  EINTR 은 재시도, EAGAIN 은 루프 복귀입니다. 이 세 가지를 구분하는 게 핵심입니다."
  > *"Short reads and writes are normal. A short read signals EOF, not an error; EINTR
  > means retry; EAGAIN means go back to the loop. Distinguishing those three is the job."*

**종료**
- "SIGTERM 핸들러는 `volatile sig_atomic_t` 플래그를 세우고 self-pipe 에 1바이트를 쓰는
  것만 합니다. async-signal-safe 하지 않은 일은 전부 루프에서 합니다. `sigaction` 을
  쓰되 `SA_RESTART` 는 켜지 않아 `poll` 이 EINTR 로 즉시 깨게 합니다."
  > *"The SIGTERM handler only sets a `volatile sig_atomic_t` and writes one byte to the
  > self-pipe — everything unsafe happens back in the loop. I use `sigaction` without
  > `SA_RESTART` so `poll` returns EINTR immediately."*

**전원 손실 · 재연결**
- "설정은 같은 디렉토리의 임시 파일에 쓰고 fsync 한 뒤 rename 하고, 디렉토리도 fsync 합니다.
  rename 은 원자적이라 독자는 항상 완전한 파일만 봅니다."
  > *"I write config to a temp file in the same directory, fsync it, rename it, then
  > fsync the directory. The rename is atomic, so a reader never sees a half-written file."*
- "재연결은 지수 백오프에 상한과 지터를 겁니다. 200만 대가 동시에 돌아오면 클라우드가
  두 번 죽습니다. 난수는 인자로 주입해서 순수 함수로 만들고 단위 테스트합니다."
  > *"Reconnects use exponential backoff with a cap and jitter — two million gateways
  > returning at once would take the cloud down twice. I inject the random value so the
  > policy stays a pure, unit-testable function."*

**테스트 전략 (이 팀이 명시한 평가 항목)**
- "하드웨어 없이 테스트하기 위해 모뎀을 pipe 로, 시계를 주입 가능한 함수로 바꿉니다.
  라인 파서와 백오프는 순수 함수라 I/O 없이 테스트하고, 루프는 pipe 양 끝으로 테스트합니다."
  > *"To test without hardware I replace the modem with a pipe and the clock with an
  > injectable function. The line parser and the backoff policy are pure functions, so
  > they're tested with no I/O at all; the loop itself is driven through pipe endpoints."*

---

## 11. 체크리스트

- [ ] `read_full`/`write_full` 을 보지 않고 쓸 수 있다 (EINTR·EOF·부분 전송 구분)
- [ ] `fcntl` read-modify-write 로 `O_NONBLOCK` 을 켤 수 있다 (`F_GETFL` vs `F_GETFD` 구분)
- [ ] `EAGAIN`/`EWOULDBLOCK`/`EINTR`/`EOF` 네 가지를 각각 어떻게 처리하는지 말할 수 있다
- [ ] `poll` 루프를 빈 화면에서 작성할 수 있다 (EOF fd 제거, revents 검사, 타임아웃 재계산)
- [ ] poll vs epoll vs kqueue 표를 말로 설명할 수 있다 (+ LT vs ET)
- [ ] self-pipe trick 을 3줄로 설명하고, eventfd/signalfd 대안을 말할 수 있다
- [ ] 시그널 핸들러에서 허용되는 것/금지된 것을 즉답할 수 있다
- [ ] `sigaction` vs `signal`, `SA_RESTART` 의 효과를 말할 수 있다
- [ ] 스트림에서 라인/프레임을 조립하는 상태 있는 파서를 쓸 수 있다 (오버플로 복구 포함)
- [ ] 지수 백오프 + 지터를 오버플로 없이 쓰고, 왜 난수를 주입하는지 설명할 수 있다
- [ ] `CLOCK_MONOTONIC` 을 쓰는 이유를 말할 수 있다
- [ ] write+fsync+rename+dir fsync 6단계를 순서대로 말할 수 있다
- [ ] `goto cleanup` 패턴으로 fd 누수 없는 함수를 쓸 수 있다
- [ ] `close()` EINTR 을 재시도하면 왜 위험한지 설명할 수 있다
- [ ] timerfd / signalfd / systemd sd_notify / netlink 를 각각 한 문장으로 설명할 수 있다
- [ ] `/proc/<pid>/fd` 로 fd 누수를 확인하는 방법을 안다
