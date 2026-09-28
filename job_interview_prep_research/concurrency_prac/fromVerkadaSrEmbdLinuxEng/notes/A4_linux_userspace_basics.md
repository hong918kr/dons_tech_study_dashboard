# A4. Embedded Linux 사용자 공간 기초 — 이 문제들을 이해하는 데 필요한 만큼

> **이 노트를 읽고 나면**
> - "`read()` 가 블로킹한다"를 커널 관점으로 설명할 수 있다 (스레드가 대기 큐에 들어간다)
> - fd · `O_NONBLOCK`/`EAGAIN` · `poll()` · `/dev` 를 알고, 10문제의 "다루기 불편한 벤더 API" 가 현실의 무엇을 흉내 낸 것인지 안다
> - 프로세스와 스레드의 차이를 한 표로 말하고, 시그널로 종료할 때 무엇이 제약인지 안다
>
> **선행**: 없음. 도구 쪽은 [A3. 빌드와 도구](A3_build_and_tools.md).
> **이 개념을 쓰는 문제**: 전부. 특히 [03_event_tailer_shutdown](../03_event_tailer_shutdown/question_note) (블로킹 read + 깨우기), [04_temp_single_flight](../04_temp_single_flight/question_note) (느린 I2C 장치), [10_heartbeat_watchdog](../10_heartbeat_watchdog/question_note) (타이머).

---

## 1. 왜 이게 필요한가

이 폴더의 10문제는 한 문장에서 출발한다.

> 주어진 벤더 API가 **블로킹**이거나 다루기 불편하다. 이것을 non-blocking · thread-safe API로 감싸라.

"블로킹"이 정확히 무슨 일인지 모르면 문제 자체가 안 읽힌다.
[04_temp_single_flight](../04_temp_single_flight/question_note) 의 벤더 함수는 이렇다.

```
int tempdev_read(float *celsius);     실제 부품에서 약 600 ms 블로킹
                                      NACK 여도 똑같이 600 ms 걸린다
                                      동시에 두 스레드가 들어가면 I2C 버스가 깨진다
```

면접에서 나올 질문은 "왜 600 ms 인가"가 아니라 **"600 ms 동안 무엇이 일어나고 있나"** 다. 그 답이
"내 스레드가 커널의 대기 큐에 들어가 있고 CPU 는 다른 일을 하고 있다" 여야 한다. 그걸 알면 전담 스레드
하나로 감싸는 설계가 자연스럽게 나오고, 모르면 "빨리 되게 하는 방법"만 찾다가 막힌다. 이 노트는
그 사정을 문제 푸는 데 필요한 만큼만 본다.

## 2. 그림으로 먼저

사용자 공간 / 커널 공간, 그리고 그 둘을 잇는 정수 하나 — 파일 디스크립터.

```svg
<svg viewBox="0 0 720 340" role="img" aria-label="사용자 공간과 커널 공간, fd 테이블">
  <rect class="box" x="10" y="10" width="700" height="130" rx="8"/><text x="24" y="32">사용자 공간 — 내 코드. 하드웨어를 직접 못 만진다</text>
  <rect class="fill-soft" x="30" y="44" width="330" height="84" rx="6"/><text class="lbl" x="44" y="62">fd 테이블 (프로세스마다 따로. 내용은 커널만 안다)</text>
  <rect class="box" x="44" y="72" width="44" height="22" rx="3"/><text class="lbl" x="66" y="87" text-anchor="middle">0 in</text>
  <rect class="box" x="94" y="72" width="48" height="22" rx="3"/><text class="lbl" x="118" y="87" text-anchor="middle">1 out</text>
  <rect class="box" x="148" y="72" width="48" height="22" rx="3"/><text class="lbl" x="172" y="87" text-anchor="middle">2 err</text>
  <rect class="box" x="202" y="72" width="70" height="22" rx="3"/><text class="lbl" x="237" y="87" text-anchor="middle">3 i2c-1</text>
  <rect class="box" x="278" y="72" width="70" height="22" rx="3"/><text class="lbl" x="313" y="87" text-anchor="middle">4 소켓</text>
  <rect class="box" x="390" y="44" width="300" height="84" rx="6"/><text class="lbl" x="404" y="64">read(3, buf, 64) → "3번 칸의 것에서 읽어 줘"</text>
  <text class="lbl" x="404" y="88">write(1,"hi",2) · poll(&amp;pfd,1,100) · ioctl(3,...)</text>
  <line class="dash" x1="10" y1="165" x2="710" y2="165"/><text x="24" y="158">↓ 시스템 콜 (syscall): 여기서 CPU 모드가 바뀐다</text>
  <rect class="box" x="10" y="180" width="700" height="150" rx="8"/><text x="24" y="202">커널 공간 — 특권 모드. 여기만 하드웨어를 만진다</text>
  <rect class="fill-soft" x="30" y="214" width="200" height="96" rx="6"/><text class="lbl" x="44" y="236">열린 파일 객체</text>
  <text class="lbl" x="44" y="258">offset · O_NONBLOCK · 버퍼</text>
  <text class="lbl" x="44" y="288">대기 큐 ← 스레드가 눕는 곳</text>
  <rect class="box" x="260" y="214" width="180" height="96" rx="6"/><text class="lbl" x="274" y="244">장치 드라이버</text>
  <text class="lbl" x="274" y="272">인터럽트를 받아 깨운다</text>
  <rect class="box" x="470" y="214" width="220" height="96" rx="6"/><text class="lbl" x="484" y="250">하드웨어: I2C · UART · NIC — "600 ms" 의 출처</text>
  <line class="accent" x1="237" y1="96" x2="237" y2="212"/><polygon class="accent" points="233,212 237,220 241,212"/>
  <line class="accent" x1="230" y1="262" x2="254" y2="262"/><polygon class="accent" points="254,258 262,262 254,266"/>
  <line class="accent" x1="440" y1="262" x2="464" y2="262"/><polygon class="accent" points="464,258 472,262 464,266"/>
</svg>
```

두 번째 그림. **블로킹하는 `read()` 동안 내 스레드가 어디 있는가** — 이 문제 세트의 심장이다.

```
시간 →
  스레드 A (sampler)                    CPU / 커널
  ──────────────────────────────────────────────────────────────
  read(fd, ...) 호출          ─┐  syscall 진입 → 데이터 있나? 없다
                               ▼  → A 를 fd 의 대기 큐에 넣고 SLEEPING
  (A 는 CPU 를 안 쓴다)          스케줄러가 스레드 B 를 돌린다
       .                        ... 장치 인터럽트! 데이터 도착
  깨어남  ◀───────────────────  드라이버가 대기 큐의 A 를 RUNNABLE 로
  buf 에 복사, n 반환 ◀──────    커널 버퍼 → 내 buf 로 memcpy
  ──────────────────────────────────────────────────────────────
  요점: 블로킹은 바쁘게 기다리는 게 아니라 CPU 를 놓고 눕는 것이다. 잃는 건
        CPU 가 아니라 그 스레드 하나다 → 스레드를 하나 더 두면 된다.
```

## 3. 개념 (용어를 하나씩)

### 사용자 공간 / 커널 공간

정의: 같은 CPU 가 두 특권 수준으로 도는데, 낮은 쪽이 사용자 공간(내 프로그램), 높은 쪽이 커널 공간이다.
내 프로그램이 디스크를 지우거나 남의 메모리를 읽지 못하게 막으려고 이렇게 나눠 놓았다. 그래서 내 코드는
하드웨어를 **직접** 만질 수 없고 커널에 부탁해야 한다. 베어메탈 펌웨어와 가장 다른 지점이 이것이다 —
STM32 에서는 `*(volatile uint32_t*)0x40005400 = x;` 로 I2C 레지스터를 직접 쓰지만, 리눅스 사용자
공간에서 같은 주소를 쓰면 즉시 SIGSEGV 다.

### 시스템 콜 (syscall)

정의: 커널에 일을 부탁하는 유일한 통로. 함수 호출처럼 생겼지만 **CPU 모드 전환**이 일어난다
(arm64 라면 `svc` 명령). `read`, `write`, `open`, `close`, `poll`, `ioctl`, `mmap`, `futex` 같은 것들.

`read(fd, buf, 64)` 가 하는 일을 순서대로 — ① 인자를 레지스터에 담고 `svc` 실행 → ② CPU 가 커널
모드로 바뀌고 `sys_read` 로 점프 → ③ 커널이 fd 3번이 무엇인지 찾는다 → ④ 데이터가 있으면 커널 버퍼에서
내 `buf` 로 복사, 없으면 나를 **대기 큐에 눕힌다** → ⑤ 바이트 수를 반환하고 사용자 모드로 복귀.
syscall 한 번은 수백 나노초~1 마이크로초로 함수 호출보다 100배쯤 비싸다. 그래서 하네스의 성능 검사가
"getter 에서 syscall 을 부르지 말라"는 뜻이 된다.

### 파일 디스크립터 (fd)

정의: **프로세스마다 있는 표의 인덱스인 작은 음이 아닌 정수.** 그게 전부다.
`3` 이라는 숫자 자체에 정보가 없다. 커널이 그 표의 3번 칸에 "이 파일의 어디까지 읽었고,
`O_NONBLOCK` 이 켜져 있고, 이 드라이버에 붙어 있다"를 들고 있다.

왜 이렇게 만들었나 — 사용자 공간에 커널 포인터를 절대 주지 않기 위해서다. 정수를 주면 잘못된 값을
넣어도 커널이 "그런 fd 없다"(`EBADF`)로 거절하면 된다.

`0`, `1`, `2` 는 관례로 정해져 있다.

| fd | 이름 | 보통 무엇 |
|---|---|---|
| 0 | stdin | 터미널 입력, 또는 파이프의 읽는 쪽 |
| 1 | stdout | `printf` 가 여기로 간다 |
| 2 | stderr | 에러. 버퍼링이 없어서 크래시해도 남는다 |
| 3 이상 | 내가 `open` 한 것 | **가장 작은 빈 번호**가 배정된다 |

"가장 작은 빈 번호"가 중요하다. `close(3)` 한 뒤 `open` 하면 다시 3번이 온다. 그래서 fd 를 닫고도 그
변수를 계속 쓰면 엉뚱한 장치를 읽는 버그가 된다 — 사용자 공간 판 use-after-free 다.

### "모든 게 파일"

리눅스에서 일반 파일, 터미널, 파이프, 소켓, I2C 장치, GPIO, 타이머가 **전부 fd** 다. 그래서
`read`/`write`/`close`/`poll` 이라는 같은 네 함수로 다 다룰 수 있다. 임베디드에서 이게 왜 편한가 —
온도 센서를 읽는 코드와 네트워크에서 읽는 코드가 **같은 모양**이 되고, `poll()` 하나로 "센서 데이터
또는 소켓 데이터 또는 종료 신호 중 먼저 오는 것"을 기다릴 수 있다.

### 블로킹 vs 논블로킹

**블로킹**: 데이터가 없으면 커널이 **호출한 스레드를 재운다** (위 두 번째 그림). 잃는 것은 CPU 가
아니라 그 스레드다. 그 스레드가 다른 요청도 처리해야 했다면 그게 문제다.

**논블로킹** (`O_NONBLOCK`): "기다리지 말고 지금 되는 만큼만." 데이터가 없으면 즉시 `-1` 을 반환하고
`errno` 를 `EAGAIN` 으로 둔다. `EAGAIN` 은 **에러가 아니다.** "지금은 없다, 나중에 다시 오라"는 정상
신호다. 이걸 에러로 처리하면 정상 동작하는 프로그램이 종료된다. 실제로 확인한 값:

```
논블로킹 read -> -1, errno=35 (Resource temporarily unavailable), 0.00 ms
```

macOS 에서 `EAGAIN` 은 **35**, 리눅스에서는 **11** 이다. 숫자를 외우지 말고 `errno == EAGAIN` 으로
비교해야 하는 이유다. 리눅스에는 `EWOULDBLOCK` 도 있는데 `EAGAIN` 과 같은 값이다.

여기서 이 문제 세트 전체의 출발점이 나온다.
| 벤더가 주는 것 | 내가 만들어야 하는 것 |
|---|---|
| 블로킹 함수 (600 ms, 또는 무한) | 절대 안 멈추는 getter |
| 한 번에 한 스레드만 (not thread-safe) | 아무 스레드나 불러도 되는 API |
| 타임아웃도 논블로킹 모드도 없음 | 종료 요청에 즉시 반응하는 구조 |

답은 항상 같은 모양이다 — **블로킹 호출을 전담 스레드 하나에 가두고, 그 스레드가 결과를 공유 상태에
publish 하고, 나머지는 그 공유 상태만 읽는다.** 그래서 이 연습이 Part 1(동시성)로 시작한다.

### `poll()` 한 번 훑기

`O_NONBLOCK` 으로 바꿔 놓고 계속 `read` 를 부르면 CPU 를 100% 태운다 (busy polling). 그래서 커널이
"이 fd 들 중 하나가 준비되면 깨워 줘"를 제공한다.

```c
struct pollfd pf[2];
pf[0].fd = sensor_fd;  pf[0].events = POLLIN;   /* 읽을 게 생기면 */
pf[1].fd = wake_fd;    pf[1].events = POLLIN;   /* 종료 알림용 파이프 */
int r = poll(pf, 2, 100);                        /* 최대 100 ms */
```

반환값은 **준비된 fd 개수**다. `0` 은 타임아웃, `-1` 은 에러(`EINTR` 이면 시그널에 끊긴 것이니 재시도).
어느 fd 인지는 `pf[i].revents & POLLIN` 으로 본다. 실제로 돌린 결과:

```
poll(타임아웃 100 ms) -> 0, 102 ms 뒤 복귀
poll(데이터 있음) -> 1, revents&POLLIN=1, 0.01 ms
```

이 구조가 **이벤트 루프**다. 한 스레드가 `poll` 로 눕고, 무언가 오면 깨서 처리하고, 다시 눕는다.
`pf[1]` 처럼 "종료 알리는 fd" 를 같이 넣는 관용구를 self-pipe 라 한다 (리눅스에는 더 깔끔한 `eventfd`).

[03_event_tailer_shutdown](../03_event_tailer_shutdown/question_note) 의 벤더는 fd 를 안 주고
`evsrc_read_blocking()` 과 `evsrc_wake()` 두 개만 준다. 하지만 **역할은 정확히 같다** — 앞이
`poll`+`read` 자리, 뒤가 self-pipe 자리다. 현실의 구조를 함수 두 개로 줄여 놓은 것이다.

### `/dev` 와 `/sys`

`/dev` 는 **장치 파일**이 모인 디렉토리다. 파일처럼 보이지만 디스크에 내용이 없고 `open` 하면 드라이버에
연결된다. 이 맥에도 345개가 있다. 리눅스 임베디드 보드라면 `/dev/i2c-1`, `/dev/spidev0.0`, `/dev/video0`
이 있다. I2C 온도 센서를 읽는 코드는 이런 모양이다 (**리눅스 전용** — `linux/i2c-dev.h` 가 필요해서
맥에서는 컴파일되지 않는다. 이 노트의 다른 코드는 전부 이 맥에서 컴파일·실행해 본 것이다).

```c
int fd = open("/dev/i2c-1", O_RDWR);        /* 1번 I2C 컨트롤러 */
ioctl(fd, I2C_SLAVE, 0x48);                 /* 대화할 슬레이브 주소 */
uint8_t reg = 0x00;
write(fd, &reg, 1);                          /* 레지스터 지정 */
uint8_t raw[2];
read(fd, raw, 2);                            /* 변환 결과 2바이트 */
```

`ioctl` 은 "read/write 로 표현이 안 되는 모든 것"을 위한 만능 창구다 — 슬레이브 주소, 보드레이트,
버스 속도. 04번의 `tempdev_read()` 가 **이 다섯 줄을 하나로 뭉쳐 놓은 벤더 blob** 이고 그 안의 변환
대기가 600 ms 다. "동시에 두 스레드가 들어가면 버스가 깨진다"도 여기서 나온다 — 스레드 둘이
`ioctl(주소)` 와 `read` 를 번갈아 하면 서로의 트랜잭션에 끼어든다.

`/sys` (sysfs) 는 장치의 **속성**을 텍스트 파일로 보여 준다. 읽으면 값이 나오고 쓰면 설정된다
(`cat /sys/class/thermal/thermal_zone0/temp` → `46500`, 밀리섭씨). **`/dev` 는 데이터 스트림,
`/sys` 는 설정과 상태** — 이 구분만 기억하면 된다.

### 시그널 아주 짧게

시그널은 커널이 프로세스에 보내는 비동기 알림이다. `SIGINT` 는 Ctrl-C, `SIGTERM` 은 `kill` 의 기본,
`SIGSEGV` 는 잘못된 메모리 접근. 멀티스레드에서 중요한 사실 두 가지.

- 시그널은 **프로세스에 온다.** 핸들러는 그때 막혀 있지 않은 아무 스레드 하나에서 실행된다. 어느
  스레드일지 내가 고르지 못한다.
- 기본 동작으로 프로세스가 죽으면 **모든 스레드가 그 자리에서 사라진다.** 잡고 있던 mutex 도 절반만
  쓴 파일도 그대로다. 그래서 깨끗하게 끝내려면 핸들러가 직접 정리하지 말고 **플래그만 세우고 각
  스레드가 스스로 빠져나오게** 해야 한다.

핸들러 안에서 쓸 수 있는 함수는 **async-signal-safe** 로 표시된 것들뿐이다. `printf` 는 아니다 — 내부
락을 잡으므로, 그 락을 이미 잡은 스레드에서 핸들러가 돌면 즉시 데드락이다. `write(2, "bye\n", 4)` 는
안전하다. 핸들러가 건드려도 되는 변수는 `volatile sig_atomic_t` 다. 실제로 돌린 최소 예제:

```c
static volatile sig_atomic_t stop_flag;
static void on_sigint(int sig) { (void)sig; stop_flag = 1; }   /* printf 금지 */

static void *worker(void *a) {                  /* while 조건에서 스스로 나온다 */
    (void)a;
    while (!stop_flag) { atomic_fetch_add(&worker_loops, 1); usleep(1000); }
    return NULL;
}
```

출력은 `워커가 70 번 돌고 정상 종료했다, stop_flag=1`. **`sigaction` 을 안 걸면?** — `SIGINT` 의 기본
동작으로 프로세스가 즉사한다. `pthread_join` 까지 못 가니 `./main.sh` 의 `Shutdown` 검사를 통과할 수 없다.

### 프로세스 vs 스레드

| | 프로세스 | 스레드 |
|---|---|---|
| 메모리 주소 공간 | 각자 따로. 남의 것 못 본다 | **공유.** 전역 변수·힙이 하나 |
| fd 테이블 | 각자 따로 (`fork` 때 복제) | **공유.** 한 스레드가 `close` 하면 전부 영향 |
| 만드는 비용 | 비싸다 (페이지 테이블, fd 복제) | 싸다 (스택만 새로 잡는다) |
| 통신 방법 | 파이프, 소켓, 공유 메모리, 시그널 | **그냥 같은 변수** |
| 하나가 죽으면 | 나머지는 살아남는다 | 프로세스 전체가 죽는다 |
| 동기화 필요? | 공유 메모리를 쓸 때만 | **항상.** 같은 변수를 두 스레드가 만진다 |
| 이 문제들에서 | 안 쓴다 | 이게 전부다 ([B1](B1_threads_and_scheduler.md) 에서 pthread) |

세 번째 줄과 여섯 번째 줄이 이 연습의 전부다. 스레드는 싸고 변수를 공유하니 편한데 **그래서 mutex 와
atomic 이 필요해진다.** "블로킹 호출을 전담 스레드에 가둔다"가 성립하는 이유도 스레드가 싸기 때문이다.

## 4. 코드로 보기

fd 가 정말 그냥 정수인지 직접 본다.

```c
printf("stdin=%d stdout=%d stderr=%d\n", STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO);
int fd  = open("/dev/urandom", O_RDONLY);
unsigned char buf[4]; ssize_t n = read(fd, buf, sizeof buf);
int fd2 = open("/dev/urandom", O_RDONLY);       /* 같은 파일을 또 열면? */
```

실제 출력:

```
stdin=0 stdout=1 stderr=2
open("/dev/urandom") -> fd 3        read -> 4 bytes: ff c8 74 e4
두 번째 open -> fd 4 (숫자만 다르다)
```

같은 파일을 두 번 열면 fd 가 두 개 생기고 각각 **따로** offset 과 플래그를 갖는다.
**`close(fd)` 를 안 하면?** — fd 는 프로세스당 상한이 있다 (`ulimit -n`). 안 닫으면 누수되어 어느 순간
`open` 이 `EMFILE` 로 실패한다. 임베디드에서 몇 주씩 도는 프로세스가 죽는 전형적 원인이다.

커널이 들고 있는 표를 밖에서 볼 수도 있다. `lsof -p <pid>` 의 실제 출력:

```
COMMAND   PID USER   FD   TYPE             DEVICE  NODE NAME
fds     20375 donh    0u  unix  0x774ee00c18c2c68       ->0x925ae35afb441e7b
fds     20375 donh    1   PIPE 0x62f27bda3f5dd670       ->0x12b6a87c8edace06   (2 도 같다)
fds     20375 donh    3r   CHR               17,1   612 /dev/urandom
fds     20375 donh    4w   CHR                3,2   336 /dev/null
```

`FD` 열이 §2 그림의 표 그대로다. `r`/`w` 는 열 때 준 모드, `CHR` 은 문자 장치, `17,1` 은 드라이버를
가리키는 major/minor 번호다. 여기서 stdout/stderr 가 터미널이 아니라 `PIPE` 인 것은 이 명령이
파이프로 실행됐기 때문이다 — **fd 1 이 항상 화면이 아니라는 증거**다.

## 5. 단계별로 만들어 보기 — 블로킹을 어떻게 감쌀 것인가

파이프 하나를 만들고 300 ms 뒤에 누가 써 주는 상황으로 세 버전을 이 맥에서 직접 돌렸다.

### v0 — 블로킹 `read` 를 그대로 호출한다 (틀렸다)

```c
pipe(p);
pthread_create(&t, NULL, slow_writer, NULL);     /* 300 ms 뒤에 쓴다 */
double t0 = now_ms();
ssize_t n = read(p[0], buf, sizeof buf);         /* 여기서 멈춘다 */
printf("블로킹 read -> %zd bytes, %.0f ms\n", n, now_ms() - t0);
```

```
블로킹 read -> 3 bytes, 309 ms 멈춰 있었다
```

값은 정확히 왔다. **문제는 호출한 쪽이 309 ms 사라졌다는 것.** 이게 getter 안에 있으면 "getter 는
절대 블로킹하지 않는다"는 요구를 바로 위반한다. 하네스가 `getters never block on the module` 로 잡는다.

### v1 — `O_NONBLOCK` 으로 바꿔서 계속 물어본다 (나아졌지만 나쁘다)

```c
fcntl(p[0], F_SETFL, O_NONBLOCK);
ssize_t n = read(p[0], buf, sizeof buf);      /* 즉시 돌아온다 */
```

```
논블로킹 read -> -1, errno=35 (Resource temporarily unavailable), 0.00 ms
```

이제 `read` 가 멈추지 않는다. 하지만 `while (read(...) < 0) { }` 로 감싸면 **CPU 한 코어를 100% 태운다.**
배터리로 도는 센서에서는 그것만으로 실격이고, 벤더 API 가 fd 를 안 주면 `O_NONBLOCK` 을 걸 대상조차
없다 — 이 연습의 모든 벤더가 그렇다.

### v2 — 전담 스레드가 눕고, 나머지는 공유 상태만 읽는다 (맞다)

```
  전담 스레드 하나                     getter 를 부르는 스레드들 (여럿)
  ─────────────────                   ─────────────────────────────
  for (;;) {                           gps_get_last_fix(&out):
      블로킹 벤더 호출 ← 여기서만 눕는다     lock(m); out = shared; unlock(m)
      lock(m); shared = 받은 값; unlock(m)   return   ← 복사만, 항상 마이크로초
  }
```

블로킹은 **한 스레드에만 격리**된다. getter 는 메모리 복사만 하니 syscall 도 대기도 없다.
`./main.sh sol` 이 찍는 숫자가 증거다 — `3 getter calls in a row: worst 73 us, 0 batches over 5000 us
(vendor block 100000 us)`. 벤더가 100 ms 블로킹하는데 getter 는 73 마이크로초다.

여기서부터가 동시성 노트들의 영역이다 — `shared` 를 어떻게 보호할지([B4](B4_mutex.md)), 언제 깨울지
([B5](B5_condvar_and_bounded_queue.md)), 종료를 어떻게 알릴지([B9](B9_lifecycle_and_shutdown.md)).

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
|---|---|---|---|
| `EAGAIN` 을 에러로 처리하거나 숫자로 비교 | "read failed" 로 종료, 또는 맥에서만 동작 | `EAGAIN` 은 "지금은 데이터 없음"이라는 정상 신호다. 값은 맥 35, 리눅스 11 | `if (n < 0 && errno == EAGAIN) return NO_DATA;` — 항상 이름으로 비교 |
| 논블로킹으로 바꿔 놓고 바쁜 대기 | CPU 100%, 발열, 배터리 | 데이터 없어도 계속 syscall 을 부른다 | `poll()` 로 눕거나, 블로킹 호출을 전담 스레드에 가둔다 |
| `read` 의 반환값을 확인 안 함 | 데이터가 잘려 들어오고 파싱이 깨짐 | `read` 는 **요청보다 적게** 줄 수 있다 (short read) | 루프로 원하는 만큼 채운다. `n == 0` 은 EOF, `n < 0` 은 에러 |
| `close(fd)` 후에 그 fd 변수를 계속 씀 | 엉뚱한 장치를 읽거나 `EBADF` | 번호가 재사용된다 (가장 작은 빈 번호) | 닫은 뒤 `fd = -1` 로 두고, 쓰는 곳에서 확인 |
| 시그널 핸들러에서 `printf`/`malloc` | 드물게 멈추거나 깨짐 | async-signal-safe 가 아니다 (내부 락) | 핸들러는 `volatile sig_atomic_t` 플래그만 세운다 |
| `EINTR` 을 안 다룸 | 시그널을 받은 순간 `poll`/`read` 가 실패 | 느린 syscall 은 시그널에 끊길 수 있다 | `if (errno == EINTR) continue;` 로 재시도 |

## 7. 손으로 확인하기

### 이 맥에서 바로 되는 것

```sh
ls /dev | head                        # 장치 파일들. 이 맥에 345개 있다
ls -l /dev/null /dev/urandom          # 맨 앞 c = character device
ps -o pid,stat,%cpu,rss,command -p $$ # 내 셸의 상태
ps -M <pid>                           # macOS: 스레드별 (Linux 는 ps -L)
top -o cpu                            # 실시간. q 로 나간다
lsof -p <pid>                         # 그 프로세스의 fd 테이블
ulimit -n                             # 열 수 있는 fd 개수 상한
cd 04_temp_single_flight && ./main.sh sol &   # 돌려 놓고 위 ps 로 관찰
```

`ps` 의 `STAT` 열이 §2 두 번째 그림과 직접 연결된다. `S` 는 자고 있음(대기 큐), `R` 은 실행 가능, `Z` 는
좀비. 블로킹 `read` 안에 들어간 프로세스는 `S` 로 보이고 **CPU 는 0%** 다 — "블로킹은 CPU 를 안 쓴다"를
눈으로 보는 증거다.

### macOS 와 Linux 가 다른 부분 (정직하게)

이 연습은 맥에서 한다. Verkada 의 실제 타깃은 임베디드 리눅스다. 확인해 본 차이:

| 항목 | 이 맥 (arm64, macOS 26) | Linux 타깃 |
|---|---|---|
| `/proc` | **없다.** `ls /proc` → `No such file or directory` | `/proc/<pid>/status`, `/proc/<pid>/fd/`, `/proc/interrupts` |
| `/sys` | **없다** | sysfs. GPIO·thermal·I2C 속성이 여기 |
| `/dev/i2c-*` | **없다** (I2C 를 사용자 공간에 안 노출) | `/dev/i2c-1` + `ioctl(I2C_SLAVE, ...)` |
| syscall 추적 | `dtruss` 가 있지만 **SIP 때문에 권한 부족**으로 실패한다 | `strace -f -T ./a.out`, `ltrace` |
| 깨우기용 fd | `pipe()` (self-pipe) | `pipe()` 또는 더 깔끔한 `eventfd()` |

`dtruss /bin/echo hi` 를 실제로 돌려 본 결과:

```
dtrace: system integrity protection is on, some features will not be available
dtrace: failed to initialize dtrace: DTrace requires additional privileges
```

즉 **이 맥에서 syscall 추적은 사실상 못 한다.** 그래서 이 노트의 확인 방법은 `printf` 로 시간을 재거나
`lsof`/`ps` 로 밖에서 보는 쪽에 기대고 있다. 리눅스 보드에서는
`strace -T -e trace=read,write,ioctl ./a.out` 로 각 syscall 이 몇 초 걸렸는지 바로 볼 수 있다.

중요: **위 차이들은 이 문제들을 푸는 데 영향이 없다.** 10문제의 벤더는 전부 `main.c` 안의 가짜 구현이고
`usleep` 으로 지연을 흉내 낸다. 진짜 fd 도 진짜 I2C 도 쓰지 않는다. 차이를 알아야 하는 이유는
**면접에서 "실제 리눅스에서는 어떻게 하나"를 묻기 때문**이다.

## 8. 자가 점검

```check
Q: "`read()` 가 블로킹한다"를 커널 관점으로 설명해 보라. 그 사이 CPU 는 무엇을 하나?
A: 커널이 데이터가 없음을 확인하고 호출한 스레드를 그 fd 의 대기 큐에 넣고 SLEEPING 으로 바꾼다.
   스케줄러는 그 스레드를 아예 고르지 않고 CPU 는 다른 스레드를 돌린다. 장치 인터럽트가 와서
   드라이버가 데이터를 넣으면 대기 큐의 스레드를 RUNNABLE 로 깨우고, 커널 버퍼에서 사용자 버퍼로
   복사한 뒤 반환한다. 잃는 건 CPU 가 아니라 그 스레드 하나다.
```

```check
Q: 논블로킹 `read` 가 `-1` 을 반환했다. 실패로 처리해야 하나?
A: `errno` 를 봐야 한다. `EAGAIN`(맥 35, 리눅스 11) 이면 실패가 아니라 "지금은 데이터가 없다"는 정상
   신호이므로 다음 기회에 다시 부른다. `EINTR` 이면 시그널에 끊긴 것이니 재시도한다. 그 외
   (`EBADF`, `EIO`)가 진짜 에러다. 숫자로 비교하지 말고 이름으로 비교한다.
```

```check
Q: fd `3` 이라는 숫자 안에는 무슨 정보가 들어 있나?
A: 아무것도 없다. 프로세스마다 하나씩 있는 표의 인덱스일 뿐이고, 그 칸에 무엇이 붙어 있는지(드라이버,
   어디까지 읽었는지, `O_NONBLOCK` 여부)는 커널만 안다. 사용자 공간에 커널 포인터를 주지 않으려고 이렇게
   만들었다. 그래서 `close` 뒤 번호가 재사용되고, 닫은 fd 를 계속 쓰면 엉뚱한 장치를 읽을 수 있다.
```

```check
Q: 04번의 벤더 `tempdev_read()` 는 실제 리눅스에서 무엇을 하고 있는 함수인가? 왜 "한 번에 한 스레드"인가?
A: `/dev/i2c-1` 을 열고 `ioctl(I2C_SLAVE, 0x48)` 로 슬레이브를 지정한 뒤 레지스터를 `write` 하고
   결과를 `read` 하는 시퀀스를, 변환 대기(약 600 ms)까지 포함해 하나로 뭉친 것이다. 이 시퀀스 전체가
   한 트랜잭션이라 스레드 둘이 끼어들면 주소 지정과 읽기가 섞여 버스가 깨진다. 감싸는 쪽이 동시
   진입을 막아야 한다.
```

```check
Q: Ctrl-C 로 프로그램을 끝낼 때 스레드들은 어떻게 되나? 깨끗하게 끝내려면?
A: 핸들러를 안 걸면 프로세스가 죽고 모든 스레드가 그 자리에서 사라진다 — mutex 를 잡은 채로, 파일을 절반만
   쓴 채로. 깨끗하게 끝내려면 `sigaction` 으로 `SIGINT` 를 받아 `volatile sig_atomic_t` 플래그만 세우고 각
   워커가 루프 조건에서 스스로 나오게 한 뒤 `pthread_join` 한다. 핸들러 안의 `printf`/`malloc` 은 금지다.
```

## 9. 요약 카드

| 기억할 것 | 한 줄 |
|---|---|
| syscall | 내 코드는 하드웨어를 직접 못 만진다. 커널에 부탁한다 (함수 호출의 100배 비용) |
| fd | 프로세스별 표의 인덱스인 정수. 0/1/2 는 표준 입출력, 내 것은 3부터. 파이프·소켓·I2C·타이머도 다 fd |
| 블로킹 | 바쁘게 기다리는 게 아니라 커널 대기 큐에 눕는 것. CPU 는 0%, 잃는 건 스레드 하나 |
| `O_NONBLOCK` | 즉시 `-1` + `EAGAIN`. **에러가 아니다.** 맥 35 / 리눅스 11 |
| `poll` | 여러 fd 를 한 스레드로 기다린다. 반환값은 준비된 개수, `0` 은 타임아웃 |
| 이 문제 세트의 해법 | 블로킹은 전담 스레드에 가두고, 공유 상태에 publish, getter 는 복사만 |
| `/dev` vs `/sys` | `/dev` 는 데이터 스트림 + `ioctl`, `/sys` 는 설정·상태 텍스트 |
| 시그널 | 프로세스에 온다. 핸들러는 `volatile sig_atomic_t` 플래그만. `printf` 금지 |
| 프로세스 vs 스레드 | 스레드는 메모리와 fd 를 공유하고 만들기 싸다. 그래서 동기화가 항상 필요하다 |
| macOS 차이 | `/proc`·`/sys`·`/dev/i2c-*` 없음, `dtruss` 는 SIP 로 막힘, `gdb` 대신 `lldb` |
