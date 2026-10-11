# A4. I/O 다중화 — select / poll / epoll / eventfd / timerfd

> **이 노트를 다 읽으면**: 스레드 하나로 센서 fd·소켓·타이머·종료 신호를 동시에 기다리는 이벤트 루프를 짤 수 있다 · select/poll/epoll의 차이와 비용을 숫자로 설명할 수 있다 · level/edge trigger 함정을 피할 수 있다 · 드라이버의 `.poll`이 epoll을 깨우는 경로를 그릴 수 있다
> **왜 필요한가**: "센서 데몬을 어떻게 설계하나", "epoll이 왜 빠른가", "드라이버에서 poll을 어떻게 지원하나"는 Embedded Linux 면접 단골이고, 실무 데몬 코드의 뼈대가 전부 이벤트 루프다
> **Don 기준 난이도**: 이벤트 플래그·WFI 루프·인터럽트 소스 묶기는 이미 안다 / 이것을 fd와 커널 wait queue로 표현하는 방법, edge-trigger 함정, 커널 내부 콜백 경로는 새로 배운다
> **선행 노트**: A1(fd와 VFS), A3(blocking vs non-blocking, `O_NONBLOCK`)

---

## 0. 큰 그림 — 이게 왜 필요한가

센서 키트 데몬을 하나 떠올려 보자. 이 프로세스는 동시에 여러 가지를 기다린다.

- IMU 샘플이 IIO 버퍼(`/dev/iio:device0`)에 들어왔나
- LiDAR UDP 패킷이 소켓에 도착했나
- 100 Hz 샘플링 주기가 돌아왔나 (타이머)
- systemd가 "이제 종료해" (SIGTERM)를 보냈나
- 워커 스레드가 "캘리브레이션 끝났어"라고 알렸나

`read()`는 fd **하나**에서만 막혀서 기다린다. IMU fd에서 `read()`하며 잠들어 있으면 LiDAR 패킷이 와도 모른다. 그래서 "**여러 fd 중 아무거나 준비되면 깨워 줘**"라는 syscall이 필요하다. 이것이 **I/O 다중화(I/O multiplexing)** 이고, Linux에서는 `select` → `poll` → `epoll` 순서로 발전했다. 여기에 "타이머·스레드 알림·시그널까지 fd로 만들어서 같은 루프에 넣자"는 아이디어가 `timerfd`, `eventfd`, `signalfd`다.

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="24" font-size="14">Linux 센서 데몬: 스레드 하나가 여러 fd를 한 번에 기다린다</text>
<rect x="20" y="40" width="210" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="32" y="62" font-size="13">IMU  /dev/iio:device0</text>
<rect x="20" y="84" width="210" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="32" y="106" font-size="13">LiDAR  UDP socket</text>
<rect x="20" y="128" width="210" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="32" y="150" font-size="13">100 Hz 샘플 timerfd</text>
<rect x="20" y="172" width="210" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="32" y="194" font-size="13">종료 요청 signalfd</text>
<line x1="230" y1="57" x2="288" y2="95" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="230" y1="101" x2="288" y2="110" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="230" y1="145" x2="288" y2="125" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="230" y1="189" x2="288" y2="140" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="290" y="60" width="180" height="110" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="380" y="95" font-size="13" text-anchor="middle">epoll 인스턴스</text>
<text x="380" y="118" font-size="13" text-anchor="middle">관심 목록 (rb-tree)</text>
<text x="380" y="141" font-size="13" text-anchor="middle">ready list</text>
<line x1="470" y1="115" x2="508" y2="115" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="510" y="70" width="155" height="90" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="587" y="100" font-size="13" text-anchor="middle">이벤트 루프 스레드</text>
<text x="587" y="122" font-size="13" text-anchor="middle">epoll_wait()에서</text>
<text x="587" y="142" font-size="13" text-anchor="middle">잠들어 있다</text>
<line x1="20" y1="222" x2="665" y2="222" stroke="#888" stroke-dasharray="5 4"/>
<text x="20" y="246" font-size="13">Bare-metal 비유 (Don이 아는 세계)</text>
<rect x="20" y="258" width="210" height="36" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="32" y="281" font-size="13">IRQ: UART, SPI, TIMER</text>
<line x1="230" y1="276" x2="288" y2="276" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="290" y="258" width="180" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="380" y="281" font-size="13" text-anchor="middle">RTOS event flags</text>
<line x1="470" y1="276" x2="508" y2="276" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="510" y="258" width="155" height="36" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="587" y="281" font-size="13" text-anchor="middle">WFI → flag 검사</text>
<text x="20" y="330" font-size="13">fd = 인터럽트 소스, epoll = event group, epoll_wait = WFI + flag 검사</text>
</svg>
```
그림 1 — 위: Linux 이벤트 루프. 아래: Don이 펌웨어에서 쓰던 "IRQ가 플래그를 세우고, main loop가 WFI로 자다가 깨서 플래그를 검사"하는 구조. 모양이 같다.

**Bare-metal 비유로 한 번 더**: SSD 펌웨어에서 NVMe doorbell, NAND 완료, DMA 완료, 타이머 인터럽트를 한 개의 main loop에서 처리했을 것이다. ISR은 짧게 플래그만 세우고(`pending |= BIT_DMA_DONE`), main loop는 `WFI`로 자다가 깨어나면 어떤 비트가 섰는지 보고 핸들러를 부른다. FreeRTOS라면 `xEventGroupWaitBits()`로 여러 비트를 한 번에 기다린다. Linux에서는

- 인터럽트 소스 → **fd** (장치 파일, 소켓, 파이프, timerfd …)
- 플래그 레지스터 → 커널이 fd마다 계산하는 **준비 상태 mask** (`EPOLLIN`, `EPOLLOUT` …)
- `WFI` + 비트 검사 → `epoll_wait()` (잠들었다가, 준비된 fd 목록을 돌려받음)
- ISR이 플래그를 세움 → 드라이버가 **wait queue를 깨움** (`wake_up()`)

이 노트의 실습 환경은 Lima Ubuntu VM이다. 커널 버전은 다음과 같다.

```sh
limactl shell elinux -- uname -r
```
```text
7.0.0-34-generic
```

모든 예제는 Mac 쪽 `~/.elinux_scratch/A4/`에 소스를 두고 VM의 `/tmp/A4/`에 빌드한다. 예를 들어:

```sh
limactl shell elinux -- bash -lc 'mkdir -p /tmp/A4 && gcc -std=gnu11 -Wall -Wextra -O2 -pthread /Users/donh/.elinux_scratch/A4/ex1_select.c -o /tmp/A4/ex1 && cd /tmp/A4 && ./ex1'
```

---

## 1. 두 가지 설계 — blocking + 스레드 여러 개 vs non-blocking + 이벤트 루프

### 1.1 문제를 직접 겪어 보기

스레드 하나가 blocking `read()`로 IMU와 GNSS를 "차례로" 읽으면 어떻게 되는지 확인하는 코드다. GNSS는 100 ms에, IMU는 800 ms에 도착한다.

```c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>

static double now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

int main(void) {
    int imu[2], gnss[2];
    if (pipe(imu) < 0 || pipe(gnss) < 0) { perror("pipe"); exit(1); }
    double t0 = now_ms();
    if (fork() == 0) {
        usleep(100000); if (write(gnss[1], "G", 1) < 0) _exit(1);  /* GNSS는 100ms에 도착 */
        usleep(700000); if (write(imu[1], "I", 1) < 0) _exit(1);   /* IMU는 800ms에 도착 */
        _exit(0);
    }
    char c;
    if (read(imu[0], &c, 1) != 1) { perror("read imu"); exit(1); }    /* 여기서 막힌다 */
    printf("[%6.1f ms] imu  처리\n", now_ms() - t0);
    if (read(gnss[0], &c, 1) != 1) { perror("read gnss"); exit(1); }
    printf("[%6.1f ms] gnss 처리 (실제 도착은 100ms → 700ms 늦음)\n", now_ms() - t0);
    wait(NULL);
    return 0;
}
```

```text
[ 818.8 ms] imu  처리
[ 818.9 ms] gnss 처리 (실제 도착은 100ms → 700ms 늦음)
```

출력에서 볼 것: GNSS 데이터는 100 ms에 이미 와 있었지만 스레드가 IMU `read()`에 묶여 있어서 700 ms 늦게 처리됐다. 펌웨어로 치면 "UART RX 플래그를 polling하는 while 루프 안에 갇혀서 SPI 인터럽트를 놓치는" 상황이다.

### 1.2 해결책 두 가지

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<line x1="340" y1="10" x2="340" y2="310" stroke="#888" stroke-dasharray="4 4"/>
<text x="20" y="24" font-size="14">(a) blocking I/O + 스레드 여러 개</text>
<rect x="20" y="42" width="160" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="32" y="64" font-size="13">T1: read(imu)</text>
<rect x="195" y="42" width="125" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="257" y="64" font-size="13" text-anchor="middle">커널에서 잠듦</text>
<rect x="20" y="88" width="160" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="32" y="110" font-size="13">T2: read(gnss)</text>
<rect x="195" y="88" width="125" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="257" y="110" font-size="13" text-anchor="middle">커널에서 잠듦</text>
<rect x="20" y="134" width="160" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="32" y="156" font-size="13">T3: recv(lidar)</text>
<rect x="195" y="134" width="125" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="257" y="156" font-size="13" text-anchor="middle">커널에서 잠듦</text>
<line x1="100" y1="170" x2="100" y2="192" stroke="currentColor" marker-end="url(#ah2)"/>
<rect x="20" y="194" width="300" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="170" y="216" font-size="13" text-anchor="middle">공유 상태 + mutex (락 필요)</text>
<text x="20" y="256" font-size="13">스레드마다 스택(기본 8 MB 가상) + 문맥 전환</text>
<text x="20" y="278" font-size="13">공유 데이터마다 락, 데드락·경쟁 위험</text>
<text x="20" y="300" font-size="13">장점: 코드가 순차적이라 읽기 쉽다</text>
<text x="360" y="24" font-size="14">(b) non-blocking + 이벤트 루프</text>
<rect x="360" y="42" width="110" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="415" y="62" font-size="13" text-anchor="middle">imu fd</text>
<rect x="360" y="80" width="110" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="415" y="100" font-size="13" text-anchor="middle">gnss fd</text>
<rect x="360" y="118" width="110" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="415" y="138" font-size="13" text-anchor="middle">lidar fd</text>
<rect x="360" y="156" width="110" height="30" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="415" y="176" font-size="13" text-anchor="middle">timerfd</text>
<line x1="470" y1="57" x2="508" y2="100" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="470" y1="95" x2="508" y2="110" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="470" y1="133" x2="508" y2="120" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="470" y1="171" x2="508" y2="130" stroke="currentColor" marker-end="url(#ah2)"/>
<rect x="510" y="70" width="150" height="80" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="585" y="104" font-size="13" text-anchor="middle">스레드 1개</text>
<text x="585" y="126" font-size="13" text-anchor="middle">epoll_wait()</text>
<line x1="585" y1="150" x2="585" y2="192" stroke="currentColor" marker-end="url(#ah2)"/>
<rect x="510" y="194" width="150" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="585" y="216" font-size="13" text-anchor="middle">핸들러 차례로 실행</text>
<text x="360" y="256" font-size="13">스레드 1개 → 락이 거의 필요 없다</text>
<text x="360" y="278" font-size="13">핸들러는 짧게, 절대 block 금지</text>
<text x="360" y="300" font-size="13">fd 수천 개도 같은 비용 (epoll)</text>
</svg>
```
그림 2 — (a) 소스마다 스레드를 하나씩 두고 각자 blocking. (b) 모든 fd를 non-blocking으로 두고 한 스레드가 "준비된 것만" 처리.

| 항목 | (a) blocking + 스레드 | (b) non-blocking + 이벤트 루프 |
|---|---|---|
| 코드 모양 | 순차적, 읽기 쉬움 | 콜백·상태 머신, 처음엔 낯섦 |
| 자원 | 스레드마다 스택·TCB(`task_struct`) | 스레드 1개 + epoll 인스턴스 1개 |
| 동기화 | 공유 데이터에 mutex 필요 | 같은 스레드라 대부분 불필요 |
| 지연 | 스케줄러가 깨워 줌 | 앞 핸들러가 길면 뒤 이벤트가 밀림 |
| 확장성 | 소스 수천 개면 스레드 수천 개 | fd 수천 개도 OK (epoll) |
| 펌웨어 비유 | RTOS 태스크를 소스마다 하나씩 | super-loop + event flags |

실무에서는 섞어 쓴다. **이벤트 루프 하나가 I/O 준비 여부를 보고, 무거운 일(이미지 압축, 파일 쓰기, 긴 계산)은 worker 스레드로 보내고, 끝나면 eventfd로 루프에 알린다.** 이 노트의 끝에서 그런 루프를 실제로 만든다(§8).

> 용어: **non-blocking fd** = `O_NONBLOCK`이 켜진 fd. 데이터가 없으면 잠들지 않고 `-1` + `errno == EAGAIN`으로 즉시 돌아온다(A3). 이벤트 루프에서는 "준비됐다는 알림을 받은 뒤 읽는데, 혹시 모를 경우에도 절대 막히지 않게" 하려고 fd를 non-blocking으로 둔다.

---

## 2. select — 가장 오래된 방법

### 2.1 정의

```c
int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
```

- `fd_set`은 **비트맵**이다. fd 번호 = 비트 번호. 크기는 `FD_SETSIZE`(=1024) 비트로 고정.
- `FD_ZERO`, `FD_SET(fd, &set)`, `FD_CLR`, `FD_ISSET`은 그 비트를 조작하는 매크로다. 펌웨어의 `reg |= (1u << n)`과 똑같다.
- `nfds`는 "가장 큰 fd 번호 + 1". 커널은 비트 0부터 `nfds-1`까지 전부 훑는다.
- **in-out 인자**: 호출 전에는 "관심 있는 fd", 리턴 후에는 "준비된 fd"만 남는다. 그래서 루프를 돌 때마다 **다시 채워야** 한다.

```
fd_set (1024 bits = 128 bytes)
bit:  0 1 2 3 4 5 6 7 ... 1023
      0 0 0 1 0 1 0 0 ...  0     <- before: watch fd 3, fd 5
      0 0 0 1 0 0 0 0 ...  0     <- after select(): only fd 3 ready
```

### 2.2 예제 — 두 pipe를 select로 기다리기

자식 프로세스가 300 ms에 IMU, 600 ms에 GNSS 데이터를 쓴다. 부모는 select 한 번으로 둘을 기다린다.

```c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <time.h>

static double now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

int main(void) {
    int imu[2], gnss[2];
    if (pipe(imu) < 0 || pipe(gnss) < 0) { perror("pipe"); exit(1); }
    if (fork() == 0) {                      /* child = 센서 흉내 */
        usleep(300000); if (write(imu[1], "imu", 3) < 0) _exit(1);
        usleep(300000); if (write(gnss[1], "gnss", 4) < 0) _exit(1);
        _exit(0);
    }
    printf("FD_SETSIZE=%d, imu fd=%d, gnss fd=%d\n", FD_SETSIZE, imu[0], gnss[0]);
    double t0 = now_ms();
    for (int round = 0; round < 3; round++) {
        fd_set rfds;
        FD_ZERO(&rfds);                     /* 매번 다시 채워야 한다 */
        FD_SET(imu[0], &rfds);
        FD_SET(gnss[0], &rfds);
        int maxfd = imu[0] > gnss[0] ? imu[0] : gnss[0];
        struct timeval tv = { .tv_sec = 0, .tv_usec = 500000 };
        int n = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (n < 0) { perror("select"); exit(1); }
        printf("[%6.1f ms] select()=%d, 남은 timeout=%ld us\n",
               now_ms() - t0, n, (long)tv.tv_usec);
        char buf[16];
        if (FD_ISSET(imu[0], &rfds)) {
            ssize_t r = read(imu[0], buf, sizeof buf);
            printf("   imu ready: %.*s\n", (int)r, buf);
        }
        if (FD_ISSET(gnss[0], &rfds)) {
            ssize_t r = read(gnss[0], buf, sizeof buf);
            printf("   gnss ready: %.*s\n", (int)r, buf);
        }
        if (n == 0) printf("   timeout (아무 것도 안 옴)\n");
    }
    wait(NULL);
    return 0;
}
```

```text
FD_SETSIZE=1024, imu fd=3, gnss fd=5
[ 302.7 ms] select()=1, 남은 timeout=197302 us
   imu ready: imu
[ 607.2 ms] select()=1, 남은 timeout=195610 us
   gnss ready: gnss
[1117.9 ms] select()=0, 남은 timeout=0 us
   timeout (아무 것도 안 옴)
```

출력에서 볼 것: 300 ms 근처에서 IMU만, 600 ms 근처에서 GNSS만 깨어났다(§1의 700 ms 지연이 사라졌다). **Linux의 select는 `timeout`을 "남은 시간"으로 덮어쓴다**(500 ms 중 약 197 ms 남음). 다른 OS는 안 그럴 수 있어서, 이식성 있는 코드는 루프마다 `tv`를 다시 설정한다(위 코드처럼).

### 2.3 strace로 보면 — arm64에는 select syscall이 없다

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A4 && strace -f -e trace=select,pselect6 ./ex1 2>&1 | grep pselect6 | head -3'
```
```text
[pid 189445] pselect6(6, [3 5], NULL, NULL, {tv_sec=0, tv_nsec=500000000}, NULL) = 1 (in [3], left {tv_sec=0, tv_nsec=198707415})
[pid 189445] pselect6(6, [3 5], NULL, NULL, {tv_sec=0, tv_nsec=500000000}, NULL) = 1 (in [5], left {tv_sec=0, tv_nsec=193748615})
[pid 189445] pselect6(6, [3 5], NULL, NULL, {tv_sec=0, tv_nsec=500000000}, NULL <unfinished ...>
```

출력에서 볼 것: aarch64 커널에는 옛날 `select` syscall 번호가 없고, glibc가 `pselect6`로 바꿔 호출한다(poll도 `ppoll`로, epoll_wait도 `epoll_pwait`로 — §8에서 확인). 마지막 줄은 자식이 종료하며 보낸 SIGCHLD에 잠깐 끊겼다가 커널이 자동 재시작한 흔적이다(strace에 `ERESTARTNOHAND`로 보인다; 시그널은 A6). x86_64에서 같은 프로그램을 strace하면 `select`/`poll`/`epoll_wait`가 그대로 보인다. 보드에서 strace 필터를 걸 때 이 이름 차이를 모르면 "아무것도 안 잡힌다"고 헤맨다.

### 2.4 select의 한계

| 한계 | 설명 |
|---|---|
| `FD_SETSIZE` = 1024 | fd 번호가 1024 이상이면 `FD_SET` 자체가 배열 밖을 쓴다(정의되지 않은 동작). Ubuntu gcc는 `-O2`에서 `_FORTIFY_SOURCE=3`이 기본이라 런타임 검사에 걸려 abort할 수 있다 |
| 매번 재무장 | 결과가 입력을 덮어쓰므로 루프마다 `FD_ZERO` + `FD_SET` N번 |
| O(maxfd) 스캔 | 커널은 0..nfds-1 비트를 전부 확인하고, 각 fd의 `.poll`을 호출한다. 리턴 후 유저도 다시 전부 `FD_ISSET`으로 훑는다 |
| 정보가 적음 | 읽기/쓰기/예외 세 집합뿐. HUP과 ERR을 구분하기 어렵다 |

> 이 숫자를 확인하려면 `echo | gcc -O2 -dM -E - | grep -i fortify` 를 VM에서 돌려 보면 된다. 출력: `#define _FORTIFY_SOURCE 3`.

**언제 아직 쓰나**: 아주 오래된 코드, 이식성(Windows 소켓 포함)이 필요한 라이브러리, fd 한두 개짜리 간단한 타임아웃. 새 Linux 코드에서는 poll이나 epoll을 쓴다.

---

## 3. poll — 배열 기반, 1024 제한 없음

### 3.1 정의

```c
struct pollfd {
    int   fd;       /* 감시할 fd (음수면 무시) */
    short events;   /* 관심 있는 이벤트 (입력) */
    short revents;  /* 실제로 일어난 이벤트 (출력, 커널이 채움) */
};
int poll(struct pollfd *fds, nfds_t nfds, int timeout_ms);  /* -1 = 무한, 0 = 즉시 */
```

select와의 차이:

- 입력(`events`)과 출력(`revents`)이 **다른 필드**라서 재무장이 필요 없다. 같은 배열을 계속 넘기면 된다.
- fd 번호 제한이 없다(배열 길이 = `nfds`).
- 이벤트 종류가 풍부하다.

| 플래그 | 뜻 | `events`에 넣어야 하나 |
|---|---|---|
| `POLLIN` | 읽을 데이터 있음 (또는 EOF) | 넣는다 |
| `POLLOUT` | 지금 쓰면 막히지 않음 | 넣는다 |
| `POLLPRI` | "예외적 조건": TCP urgent data, sysfs 속성 변경 알림(`sysfs_notify`), cgroup.events 변경 등 | 넣는다 |
| `POLLRDHUP` | 스트림 소켓 상대가 쓰기 쪽을 닫음 (Linux 전용, `_GNU_SOURCE`) | 넣는다 |
| `POLLERR` | 에러 조건 (예: 읽는 쪽이 닫힌 pipe에 쓰기) | 넣지 않아도 항상 보고 |
| `POLLHUP` | 끊김 (예: pipe writer가 모두 닫힘) | 넣지 않아도 항상 보고 |
| `POLLNVAL` | fd가 열려 있지 않음 | 넣지 않아도 항상 보고 |

### 3.2 예제 — revents가 상황별로 어떻게 켜지나

pipe 하나로 8가지 상황을 만들고, `poll(…, 0)`(즉시 확인)으로 revents를 찍어 보는 코드다.

```c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

static void show(const char *what, int fd, short ev) {
    struct pollfd p = { .fd = fd, .events = ev };
    int n = poll(&p, 1, 0);                  /* timeout 0 = 즉시 확인 */
    if (n < 0) { perror("poll"); exit(1); }
    printf("%-32s n=%d revents=0x%03x%s%s%s%s%s\n", what, n, p.revents,
           p.revents & POLLIN   ? " POLLIN"   : "",
           p.revents & POLLOUT  ? " POLLOUT"  : "",
           p.revents & POLLERR  ? " POLLERR"  : "",
           p.revents & POLLHUP  ? " POLLHUP"  : "",
           p.revents & POLLNVAL ? " POLLNVAL" : "");
}

int main(void) {
    int fd[2];
    if (pipe2(fd, O_NONBLOCK) < 0) { perror("pipe2"); exit(1); }
    show("1) empty pipe, read end", fd[0], POLLIN);
    show("2) empty pipe, write end", fd[1], POLLOUT);
    if (write(fd[1], "x", 1) != 1) { perror("write"); exit(1); }
    show("3) 1 byte written, read end", fd[0], POLLIN);
    char big[4096] = {0}; long total = 1;
    while (write(fd[1], big, sizeof big) > 0) total += sizeof big;
    printf("   (pipe를 가득 채움: 약 %ld bytes)\n", total);
    show("4) full pipe, write end", fd[1], POLLOUT);
    close(fd[1]);
    show("5) writer closed, read end", fd[0], POLLIN);
    char tmp[4096];
    while (read(fd[0], tmp, sizeof tmp) > 0) ;
    show("6) drained after close, read end", fd[0], POLLIN);
    int p2[2];
    if (pipe(p2) < 0) { perror("pipe"); exit(1); }
    close(p2[0]);
    show("7) reader closed, write end", p2[1], POLLOUT);
    show("8) fd 77 (never opened)", 77, POLLIN);
    return 0;
}
```

```text
1) empty pipe, read end          n=0 revents=0x000
2) empty pipe, write end         n=1 revents=0x004 POLLOUT
3) 1 byte written, read end      n=1 revents=0x001 POLLIN
   (pipe를 가득 채움: 약 61441 bytes)
4) full pipe, write end          n=0 revents=0x000
5) writer closed, read end       n=1 revents=0x011 POLLIN POLLHUP
6) drained after close, read end n=1 revents=0x010 POLLHUP
7) reader closed, write end      n=1 revents=0x00c POLLOUT POLLERR
8) fd 77 (never opened)          n=1 revents=0x020 POLLNVAL
```

출력에서 볼 것:

- 5)처럼 **writer가 닫혀도 남은 데이터가 있으면 `POLLIN | POLLHUP`이 같이** 온다. HUP을 보자마자 fd를 닫으면 마지막 데이터를 잃는다. "HUP이면 먼저 읽을 수 있는 만큼 읽고, `read()==0`(EOF)을 보고 닫는다"가 정석이다.
- 6) 다 읽은 뒤에는 `POLLHUP`만 남는다. 이걸 무시하고 루프를 돌면 poll이 계속 즉시 리턴해서 **CPU 100% 루프**가 된다(흔한 버그).
- 7) 읽는 쪽이 없는 pipe에 쓰기 → `POLLERR`. 실제로 `write()`하면 SIGPIPE/`EPIPE`다(A6).
- 8) `POLLNVAL`은 "닫힌 fd를 배열에 남겨 둔" 버그의 신호다.
- 4) pipe 용량은 기본 64 KiB(16페이지)다. 1바이트가 이미 한 슬롯을 차지해 4 KiB 쓰기 15번에서 멈췄다.

### 3.3 POLLPRI와 sysfs GPIO — "edge 인터럽트를 유저에서 기다리기"

sysfs 속성 파일(`/sys/...`)은 일반 데이터 스트림이 아니다. 그래서 **"값이 바뀌었다"는 알림은 `POLLIN`이 아니라 `POLLPRI | POLLERR`로** 온다. 드라이버가 `sysfs_notify()`(내부적으로 `kernfs_notify()`)를 부르면, 그 파일을 poll 중인 프로세스가 깨어난다. 대표 예가 레거시 sysfs GPIO의 `value` 파일이다: `edge`에 `rising`/`falling`/`both`를 쓰면 GPIO 인터럽트가 걸릴 때마다 `sysfs_notify`가 불린다.

> 주의: sysfs GPIO(`/sys/class/gpio`)는 **폐기 예정(deprecated)** 인터페이스다. 새 코드는 GPIO character device(`/dev/gpiochipN`, libgpiod)를 쓴다. 거기서는 line request fd가 `POLLIN`으로 깨어나고 `read()`로 `struct gpio_v2_line_event`를 받는다(H4). 하지만 오래된 BSP·현장 스크립트에 sysfs GPIO가 아직 많고, "sysfs 파일은 POLLPRI"라는 규칙은 다른 sysfs 속성에도 똑같이 적용되므로 여기서 직접 확인한다.

VM에는 실제 GPIO 핀이 없으므로 커널의 **gpio-sim**(가상 GPIO 칩, configfs로 만든다)을 쓴다. `pull` 속성을 바꾸면 입력 라인 값이 바뀌고, 진짜 핀처럼 edge 인터럽트가 발생한다.

```sh
# VM 안에서 (sudo 필요)
sudo modprobe gpio-sim
sudo mkdir -p /sys/kernel/config/gpio-sim/a4sim/bank0
echo 8 | sudo tee /sys/kernel/config/gpio-sim/a4sim/bank0/num_lines
echo 1 | sudo tee /sys/kernel/config/gpio-sim/a4sim/live
for c in /sys/class/gpio/gpiochip*; do echo $c $(cat $c/label) $(cat $c/base); done
```
```text
8
1
/sys/class/gpio/gpiochip512 ARMH0061:00 512
/sys/class/gpio/gpiochip520 gpio-sim.0:node0 520
```

gpio-sim 칩의 base가 520이므로 라인 3 = 전역 GPIO 523이다(base 값은 환경마다 다르다). sysfs로 export하고 edge를 켠다.

```sh
echo 523  | sudo tee /sys/class/gpio/export
echo in   | sudo tee /sys/class/gpio/gpio523/direction
echo both | sudo tee /sys/class/gpio/gpio523/edge
cat /sys/class/gpio/gpio523/value
```
```text
523
in
both
0
```

이제 `value` 파일을 POLLPRI로 기다리는 코드다.

```c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>

static double now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/sys/class/gpio/gpio523/value";
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror("open"); exit(1); }
    char v[4];
    if (pread(fd, v, sizeof v, 0) < 0) { perror("pread"); exit(1); } /* 현재 값을 먼저 읽어 둔다 */
    printf("initial value=%c\n", v[0]);
    double t0 = now_ms();
    for (int i = 0; i < 4; i++) {
        struct pollfd p = { .fd = fd, .events = POLLPRI | POLLERR };
        int n = poll(&p, 1, 3000);
        if (n < 0) { perror("poll"); exit(1); }
        if (n == 0) { printf("[%7.1f ms] timeout\n", now_ms() - t0); continue; }
        ssize_t r = pread(fd, v, sizeof v, 0);   /* offset 0부터 다시 읽는다 */
        if (r < 0) { perror("pread"); exit(1); }
        printf("[%7.1f ms] revents=0x%x value=%c\n", now_ms() - t0, p.revents, v[0]);
    }
    close(fd);
    return 0;
}
```

백그라운드로 실행해 두고, 셸에서 0.5초 간격으로 pull을 바꿔 "핀을 흔든다".

```sh
cd /tmp/A4 && (./ex3 &) ; P=/sys/devices/platform/gpio-sim.0/gpiochip1/sim_gpio3/pull
sleep 0.5; echo pull-up   | sudo tee $P >/dev/null
sleep 0.5; echo pull-down | sudo tee $P >/dev/null
sleep 0.5; echo pull-up   | sudo tee $P >/dev/null
sleep 4
```
```text
initial value=0
[  532.2 ms] revents=0xa value=1
[ 1046.0 ms] revents=0xa value=0
[ 1561.4 ms] revents=0xa value=1
[ 4567.0 ms] timeout
```

출력에서 볼 것: `revents=0xa` = `POLLPRI(0x2) | POLLERR(0x8)`. 상승·하강 edge마다 한 번씩 깨어났고, 핀을 안 건드린 마지막은 3초 timeout이다. 깨어난 뒤에는 **반드시 offset 0부터 다시 읽어야**(`lseek(fd,0,SEEK_SET)`+`read` 또는 `pread(…,0)`) 다음 변화를 정상적으로 기다린다 — sysfs GPIO 문서가 요구하는 관례다.

같은 파일을 `POLLIN`으로 기다리면 어떻게 될까? 파이썬 한 줄로 확인한다.

```sh
limactl shell elinux -- python3 -c '
import select,os
fd=os.open("/sys/class/gpio/gpio523/value",os.O_RDONLY); os.read(fd,4)
p=select.poll(); p.register(fd, select.POLLIN); print("POLLIN only:", p.poll(1000))'
```
```text
POLLIN only: [(3, 1)]
```

출력에서 볼 것: 핀을 건드리지 않았는데도 즉시 `POLLIN(1)`이 나온다. sysfs 파일은 "언제나 읽을 수 있다"고 보고하기 때문이다. **sysfs에서 POLLIN으로 기다리면 busy loop가 된다** — 현장에서 자주 보는 실수다.

실습 후 정리:

```sh
echo 523 | sudo tee /sys/class/gpio/unexport >/dev/null
echo 0 | sudo tee /sys/kernel/config/gpio-sim/a4sim/live >/dev/null
sudo rmdir /sys/kernel/config/gpio-sim/a4sim/bank0 /sys/kernel/config/gpio-sim/a4sim
```

### 3.4 poll의 남은 한계

poll은 select의 1024 제한과 재무장 문제를 없앴지만, **매 호출마다 배열 전체(N개)를 커널로 복사하고, 커널이 N개 fd의 `.poll`을 전부 호출하고, 리턴 후 유저가 N개 `revents`를 전부 훑는다.** 준비된 fd가 1개여도 비용이 O(N)이다. fd가 10개면 문제가 없고, 수천 개면 문제가 된다(§9에서 측정).

---

## 4. epoll — "등록은 한 번, 대기는 준비된 것만"

### 4.1 직관

poll은 "매번 출석부 전체를 들고 교실에 가서 한 명씩 이름을 부르는" 방식이다. epoll은 "학생 명단을 한 번 등록해 두면, 손 든 학생이 **스스로 준비 목록(ready list)에 이름을 적는**" 방식이다. 선생님(`epoll_wait`)은 준비 목록만 본다. 펌웨어로 치면 매 루프마다 모든 주변장치의 상태 레지스터를 읽는 polling 대신, **ISR이 pending 비트를 세워 두고 main loop는 pending 레지스터 하나만 읽는** 구조다.

### 4.2 API 3개

| 함수 | 역할 | 펌웨어 비유 |
|---|---|---|
| `epoll_create1(EPOLL_CLOEXEC)` | epoll 인스턴스 생성, fd 리턴 | event group 객체 생성 |
| `epoll_ctl(epfd, ADD/MOD/DEL, fd, &ev)` | 관심 목록에 fd 추가·수정·삭제 | 인터럽트 enable/mask 설정 |
| `epoll_wait(epfd, events, max, timeout)` | 준비된 fd들을 배열로 받음 | WFI 후 pending 비트 읽기 |

`struct epoll_event`는 `events`(비트 mask)와 `data`(union: `ptr`, `fd`, `u32`, `u64`)로 되어 있다. **`data`는 커널이 해석하지 않고 그대로 돌려주는 사용자 쿠키**다. 보통 `data.ptr`에 "이 fd를 처리할 객체 포인터"를 넣어 두면, 깨어났을 때 테이블 검색 없이 바로 핸들러로 간다. 펌웨어의 인터럽트 벡터 테이블 엔트리 같은 것이다.

### 4.3 예제 — 기본 3단계와 data.ptr

세 개의 pipe(imu, gnss, lidar)를 등록하고 imu와 lidar에만 데이터를 넣은 뒤, epoll이 무엇을 돌려주는지 확인한다. 마지막에 `/proc/self/fdinfo/<epfd>`로 커널이 들고 있는 관심 목록도 찍는다.

```c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>

struct source { const char *name; int rfd, wfd; };

int main(void) {
    struct source src[3] = { {"imu",0,0}, {"gnss",0,0}, {"lidar",0,0} };
    int ep = epoll_create1(EPOLL_CLOEXEC);
    if (ep < 0) { perror("epoll_create1"); exit(1); }
    for (int i = 0; i < 3; i++) {
        int p[2];
        if (pipe(p) < 0) { perror("pipe"); exit(1); }
        src[i].rfd = p[0]; src[i].wfd = p[1];
        struct epoll_event ev = { .events = EPOLLIN, .data.ptr = &src[i] };
        if (epoll_ctl(ep, EPOLL_CTL_ADD, p[0], &ev) < 0) { perror("epoll_ctl"); exit(1); }
    }
    /* imu와 lidar만 데이터가 왔다고 하자 */
    if (write(src[0].wfd, "a", 1) != 1 || write(src[2].wfd, "bb", 2) != 2) exit(1);

    struct epoll_event out[8];
    int n = epoll_wait(ep, out, 8, 1000);
    if (n < 0) { perror("epoll_wait"); exit(1); }
    printf("epoll_wait()=%d (등록 3개 중 준비된 것만 돌려준다)\n", n);
    for (int i = 0; i < n; i++) {
        struct source *s = out[i].data.ptr;
        char buf[8]; ssize_t r = read(s->rfd, buf, sizeof buf);
        printf("  %-5s fd=%d events=0x%x read=%zd\n", s->name, s->rfd, out[i].events, r);
    }
    n = epoll_wait(ep, out, 8, 200);
    printf("다시 epoll_wait()=%d (다 읽었으니 timeout)\n", n);

    char path[64], line[256];
    snprintf(path, sizeof path, "/proc/self/fdinfo/%d", ep);
    FILE *f = fopen(path, "r");
    if (!f) { perror("fopen"); exit(1); }
    printf("--- %s ---\n", path);
    while (fgets(line, sizeof line, f)) fputs(line, stdout);
    fclose(f);
    return 0;
}
```

```text
epoll_wait()=2 (등록 3개 중 준비된 것만 돌려준다)
  imu   fd=4 events=0x1 read=1
  lidar fd=8 events=0x1 read=2
다시 epoll_wait()=0 (다 읽었으니 timeout)
--- /proc/self/fdinfo/3 ---
pos:	0
flags:	02000002
mnt_id:	19
ino:	1077
tfd:        4 events:       19 data:     fffff316e678  pos:0 ino:1a4ba sdev:10
tfd:        6 events:       19 data:     fffff316e688  pos:0 ino:1a4bb sdev:10
tfd:        8 events:       19 data:     fffff316e698  pos:0 ino:1a4bc sdev:10
```

출력에서 볼 것:

- 3개를 등록했지만 `epoll_wait`는 **준비된 2개만** 돌려줬다. 유저가 N개를 훑을 필요가 없다.
- `data.ptr`로 바로 `struct source`를 찾았다.
- fdinfo의 `tfd:` 줄이 커널 관심 목록(나중에 볼 `epitem`)이다. `events: 19`(16진수) = `EPOLLIN(0x1) | EPOLLERR(0x8) | EPOLLHUP(0x10)` — 커널이 ERR/HUP을 **자동으로 추가**한다. `data:` 값은 우리가 넣은 포인터(주소는 실행마다 다르다).
- `flags: 02000002`의 `02000000`이 `O_CLOEXEC`다.

### 4.4 Level-triggered vs Edge-triggered — 가장 중요한 함정

- **Level-triggered (LT, 기본값)**: "**지금** 읽을 데이터가 남아 있으면" 매번 알려 준다. poll/select와 같은 의미다. 펌웨어의 **level-sensitive 인터럽트**(라인이 high인 동안 계속 pending)와 같다.
- **Edge-triggered (`EPOLLET`)**: "준비 상태가 **새로 생겼을 때**(새 데이터 도착 같은 변화) 한 번" 알려 준다. 펌웨어의 **edge-sensitive 인터럽트**와 같다. ISR에서 FIFO를 다 비우지 않으면 다음 edge가 올 때까지 남은 데이터가 방치되는 것과 똑같은 버그가 생긴다.

10바이트 센서 패킷이 도착했는데 핸들러가 4바이트씩만 읽는 상황을 세 가지 모드로 비교하는 코드다.

```c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>

/* mode: 0=LT, 1=ET(4바이트만 읽음, 버그), 2=ET(EAGAIN까지 drain, 정답) */
static void run(int mode) {
    const char *name[] = { "LT, read 4B", "ET, read 4B (bug)", "ET, drain to EAGAIN" };
    int p[2];
    if (pipe2(p, O_NONBLOCK) < 0) { perror("pipe2"); exit(1); }
    int ep = epoll_create1(0);
    struct epoll_event ev = { .events = EPOLLIN | (mode ? EPOLLET : 0), .data.fd = p[0] };
    if (ep < 0 || epoll_ctl(ep, EPOLL_CTL_ADD, p[0], &ev) < 0) { perror("epoll"); exit(1); }

    if (write(p[1], "0123456789", 10) != 10) exit(1);   /* 센서 패킷 10바이트 도착 */
    printf("== %s ==\n", name[mode]);
    int left = 10;
    for (int wake = 1; wake <= 5; wake++) {
        struct epoll_event out;
        int n = epoll_wait(ep, &out, 1, 100);
        if (n == 0) { printf("  wait#%d: timeout! 남은 데이터 %d바이트가 갇힘\n", wake, left); break; }
        char buf[4]; ssize_t r; int got = 0;
        do {
            r = read(p[0], buf, sizeof buf);
            if (r > 0) { got += r; left -= r; }
        } while (mode == 2 && r > 0);
        printf("  wait#%d: events=0x%x, 이번에 %d바이트 읽음, 남음 %d%s\n", wake, out.events,
               got, left, (mode == 2 && r < 0 && errno == EAGAIN) ? " (EAGAIN에서 멈춤)" : "");
        if (left == 0 && mode != 1) {
            n = epoll_wait(ep, &out, 1, 100);
            printf("  다 읽은 뒤 epoll_wait()=%d\n", n);
            break;
        }
    }
    close(p[0]); close(p[1]); close(ep);
}

int main(void) { run(0); run(1); run(2); return 0; }
```

```text
== LT, read 4B ==
  wait#1: events=0x1, 이번에 4바이트 읽음, 남음 6
  wait#2: events=0x1, 이번에 4바이트 읽음, 남음 2
  wait#3: events=0x1, 이번에 2바이트 읽음, 남음 0
  다 읽은 뒤 epoll_wait()=0
== ET, read 4B (bug) ==
  wait#1: events=0x1, 이번에 4바이트 읽음, 남음 6
  wait#2: timeout! 남은 데이터 6바이트가 갇힘
== ET, drain to EAGAIN ==
  wait#1: events=0x1, 이번에 10바이트 읽음, 남음 0 (EAGAIN에서 멈춤)
  다 읽은 뒤 epoll_wait()=0
```

출력에서 볼 것: LT는 데이터가 남아 있는 한 세 번 깨워 줬다. ET + 조금만 읽기는 **한 번만 깨우고 끝** — 남은 6바이트는 다음 패킷이 도착해야(새 edge) 비로소 처리된다. 센서라면 "가끔 한 샘플씩 늦게 나오는" 지연 버그, 소켓 서버라면 "가끔 응답이 멈추는" 버그로 나타나서 재현이 어렵다. 정답은 **ET를 쓰면 fd를 non-blocking으로 두고 `EAGAIN`이 나올 때까지 다 읽는 것**이다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="80" font-size="14">LT (level)</text>
<text x="20" y="100" font-size="12">4B씩 읽음</text>
<rect x="180" y="80" width="40" height="40" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/>
<rect x="220" y="96" width="80" height="24" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/>
<rect x="300" y="112" width="80" height="8" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/>
<line x1="150" y1="120" x2="660" y2="120" stroke="#888"/>
<line x1="220" y1="58" x2="220" y2="126" stroke="#3f9a6b" stroke-width="2"/>
<line x1="300" y1="58" x2="300" y2="126" stroke="#3f9a6b" stroke-width="2"/>
<line x1="380" y1="58" x2="380" y2="126" stroke="#3f9a6b" stroke-width="2"/>
<text x="220" y="52" font-size="12" text-anchor="middle">wait#1</text>
<text x="300" y="52" font-size="12" text-anchor="middle">wait#2</text>
<text x="380" y="52" font-size="12" text-anchor="middle">wait#3</text>
<text x="200" y="74" font-size="12" text-anchor="middle">10B</text>
<text x="260" y="90" font-size="12" text-anchor="middle">6B</text>
<text x="340" y="106" font-size="12" text-anchor="middle">2B</text>
<text x="400" y="100" font-size="12">데이터가 남아 있는 한 계속 깨움</text>
<text x="20" y="210" font-size="14">ET (edge)</text>
<text x="20" y="230" font-size="12">4B만 읽음</text>
<rect x="180" y="200" width="40" height="40" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/>
<rect x="220" y="216" width="430" height="24" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<line x1="150" y1="240" x2="660" y2="240" stroke="#888"/>
<line x1="220" y1="178" x2="220" y2="246" stroke="#3f9a6b" stroke-width="2"/>
<text x="220" y="172" font-size="12" text-anchor="middle">wait#1</text>
<text x="200" y="194" font-size="12" text-anchor="middle">10B</text>
<text x="236" y="233" font-size="12">6B가 갇힘: 새 데이터(새 edge)가 와야 다시 깨운다</text>
<line x1="150" y1="268" x2="660" y2="268" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="180" y1="264" x2="180" y2="272" stroke="currentColor"/>
<text x="180" y="288" font-size="12" text-anchor="middle">write(10B)</text>
<text x="620" y="288" font-size="12" text-anchor="middle">시간</text>
</svg>
```
그림 3 — 위: LT는 버퍼 수위(level)가 0보다 큰 동안 매번 깨운다. 아래: ET는 "비어 있다가 데이터가 생긴" 순간(edge)에 한 번만 깨운다. 덜 읽으면 남은 데이터가 갇힌다.

**언제 ET를 쓰나**: 대용량 스트림을 처리하는 고성능 서버(nginx 등)에서 불필요한 재알림을 줄이려고, 또는 여러 스레드가 같은 epoll을 공유할 때(같은 이벤트가 여러 스레드에 중복 전달되는 것을 줄이려고). 센서 데몬 정도면 **LT가 기본이고 안전**하다. ET를 쓰려면 "non-blocking + EAGAIN까지 drain"을 규칙으로 지킨다.

### 4.5 EPOLLONESHOT — "한 번 알리고 꺼짐"

여러 worker 스레드가 같은 epoll fd에서 `epoll_wait`를 할 때, 한 fd의 이벤트를 스레드 A가 처리하는 도중 같은 fd의 다음 이벤트가 스레드 B로 가면 **같은 소켓을 두 스레드가 동시에 읽는** 경쟁이 생긴다. `EPOLLONESHOT`을 주면 한 번 알린 뒤 그 fd는 비활성(disarm)되고, 처리 스레드가 끝난 뒤 `EPOLL_CTL_MOD`로 다시 켜야(re-arm) 다음 알림이 온다. 펌웨어로 치면 "ISR 진입 시 해당 인터럽트를 mask하고, 처리 끝나면 unmask"다.

```c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>

static int ep, p[2];

static void wait_once(const char *tag) {
    struct epoll_event out;
    int n = epoll_wait(ep, &out, 1, 100);
    if (n < 0) { perror("epoll_wait"); exit(1); }
    printf("%-36s epoll_wait()=%d\n", tag, n);
}

int main(void) {
    if (pipe(p) < 0) { perror("pipe"); exit(1); }
    ep = epoll_create1(0);
    struct epoll_event ev = { .events = EPOLLIN | EPOLLONESHOT, .data.fd = p[0] };
    if (ep < 0 || epoll_ctl(ep, EPOLL_CTL_ADD, p[0], &ev) < 0) { perror("epoll"); exit(1); }

    if (write(p[1], "abc", 3) != 3) exit(1);
    wait_once("1) data in, first wait");
    wait_once("2) did not read, wait again (LT?)");
    if (write(p[1], "d", 1) != 1) exit(1);
    wait_once("3) even more data arrives");
    ev.events = EPOLLIN | EPOLLONESHOT;               /* worker가 처리를 끝냈다 → 재무장 */
    if (epoll_ctl(ep, EPOLL_CTL_MOD, p[0], &ev) < 0) { perror("MOD"); exit(1); }
    wait_once("4) after EPOLL_CTL_MOD re-arm");
    return 0;
}
```

```text
1) data in, first wait               epoll_wait()=1
2) did not read, wait again (LT?)    epoll_wait()=0
3) even more data arrives            epoll_wait()=0
4) after EPOLL_CTL_MOD re-arm        epoll_wait()=1
```

출력에서 볼 것: 2)는 LT인데도 데이터가 남아 있는 상태에서 0을 돌려줬고, 3) 새 데이터가 와도 조용하다. 4) `EPOLL_CTL_MOD`로 재무장하자마자 다시 알린다. 재무장을 잊으면 그 fd는 **영원히 조용해진다**(흔한 버그).

### 4.6 EPOLLEXCLUSIVE — 짧게

여러 스레드가 **각자 자기 epoll 인스턴스**에 같은 fd(대표적으로 listen 소켓)를 등록하면, 연결 하나가 들어올 때 모든 스레드가 깨어나는 **thundering herd**(우르르 깨어났다가 한 명만 일하고 나머지는 헛걸음)가 생긴다. `EPOLLEXCLUSIVE`(Linux 4.5+)를 `EPOLL_CTL_ADD` 때 주면 커널이 그 중 **하나 이상(보통 하나)** 만 깨운다. `EPOLL_CTL_ADD`에서만 쓸 수 있고 `EPOLLONESHOT`과 함께 쓸 수 없다. 센서 데몬에서는 거의 쓸 일이 없고, 멀티스레드 네트워크 서버 면접 질문에서 이름이 나오는 정도다.

### 4.7 epoll이 못 하는 것 — 일반 파일

일반 파일을 epoll에 넣으면 어떻게 되는지 확인한다.

```c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/epoll.h>

int main(void) {
    int fd = open("/tmp/A4/regular.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
    int ep = epoll_create1(0);
    if (fd < 0 || ep < 0) { perror("open/epoll"); exit(1); }
    struct epoll_event ev = { .events = EPOLLIN, .data.fd = fd };
    if (epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev) < 0)
        printf("epoll_ctl(ADD, regular file): %s (errno=%d)\n", strerror(errno), errno);
    struct pollfd p = { .fd = fd, .events = POLLIN | POLLOUT };
    int n = poll(&p, 1, 1000);
    printf("poll(regular file, 빈 파일)=%d revents=0x%x  (기다리지 않고 즉시 리턴)\n", n, p.revents);
    return 0;
}
```

```text
epoll_ctl(ADD, regular file): Operation not permitted (errno=1)
poll(regular file, 빈 파일)=1 revents=0x5  (기다리지 않고 즉시 리턴)
```

출력에서 볼 것: 일반 파일은 `.poll` 메서드가 없어서 epoll은 `EPERM`으로 거절하고, poll은 "항상 읽기·쓰기 가능(0x5 = POLLIN|POLLOUT)"으로 답한다. 디스크 I/O가 느려도 "준비됨"이라는 뜻이다. 디스크 읽기를 진짜 비동기로 하려면 스레드 풀이나 io_uring(§9.3)을 쓴다. 로그 파일 쓰기를 이벤트 루프 안에서 하면 SD 카드가 느린 순간 루프 전체가 멈춘다.

---

## 5. eventfd — 스레드·프로세스 사이의 "초인종"

### 5.1 정의

`eventfd(initval, flags)`는 **커널 안의 64비트 카운터 하나를 fd로 감싼 것**이다.

- `write(fd, &u64, 8)`: 카운터에 값을 **더한다**.
- `read(fd, &u64, 8)`: 카운터 값을 꺼내고 **0으로 리셋**. 카운터가 0이면 blocking(또는 `EAGAIN`).
- `EFD_SEMAPHORE` 플래그: `read`가 **1씩만** 꺼낸다(세마포어처럼).
- 카운터가 0보다 크면 `POLLIN`. 그래서 epoll에 넣을 수 있다.

펌웨어 비유: 다른 코어(또는 ISR)가 공유 변수에 `count++` 하고 **SEV/doorbell 인터럽트**를 쏘는 것. 받는 쪽은 깨어나서 카운트를 원자적으로 가져가고 0으로 만든다.

pipe로도 같은 일(1바이트 쓰고 읽기, "self-pipe trick")을 할 수 있지만, eventfd는 fd 1개(pipe는 2개), 버퍼 대신 카운터 8바이트, 여러 번 써도 합쳐져서 넘칠 일이 없다는 장점이 있다.

### 5.2 예제 — 카운터 의미와 worker 스레드 알림

```c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <poll.h>
#include <sys/eventfd.h>

static void add(int fd, uint64_t v) { if (write(fd, &v, 8) != 8) { perror("write"); exit(1); } }
static long take(int fd) {
    uint64_t v; ssize_t r = read(fd, &v, 8);
    return r == 8 ? (long)v : (errno == EAGAIN ? -1 : -2);
}

static int wake_fd;
static void *worker(void *arg) {
    (void)arg;
    usleep(100000); add(wake_fd, 1); add(wake_fd, 1); add(wake_fd, 1); /* 연달아 3번 */
    usleep(100000); add(wake_fd, 1);                                    /* 한참 뒤 1번 */
    return NULL;
}

int main(void) {
    int a = eventfd(0, EFD_NONBLOCK);
    if (a < 0) { perror("eventfd"); exit(1); }
    add(a, 1); add(a, 2); add(a, 3);
    long r1 = take(a), r2 = take(a);
    printf("normal   : read=%ld, read again=%ld (-1=EAGAIN)\n", r1, r2);

    int s = eventfd(0, EFD_NONBLOCK | EFD_SEMAPHORE);
    add(s, 3);
    long x = take(s), y = take(s), z = take(s), w = take(s);
    printf("semaphore: %ld %ld %ld %ld\n", x, y, z, w);

    wake_fd = eventfd(0, 0);
    if (s < 0 || wake_fd < 0) { perror("eventfd"); exit(1); }
    pthread_t th;
    pthread_create(&th, NULL, worker, NULL);
    for (int got = 0; got < 4; ) {
        struct pollfd p = { .fd = wake_fd, .events = POLLIN };
        if (poll(&p, 1, -1) < 0) { perror("poll"); exit(1); }
        long v = take(wake_fd);
        got += v;
        printf("main loop woke: counter=%ld (total %d)\n", v, got);
    }
    pthread_join(th, NULL);
    return 0;
}
```

```text
normal   : read=6, read again=-1 (-1=EAGAIN)
semaphore: 1 1 1 -1
main loop woke: counter=3 (total 3)
main loop woke: counter=1 (total 4)
```

출력에서 볼 것:

- 일반 모드: 1+2+3이 **합쳐져서** 한 번의 read로 6이 나오고, 바로 다시 읽으면 0이라 `EAGAIN`.
- 세마포어 모드: 1씩 세 번, 네 번째는 `EAGAIN`.
- worker가 연달아 3번 알린 것은 메인 루프에서 **한 번 깨어나 counter=3**으로 받았다. 알림이 합쳐진다(coalescing). 그래서 eventfd는 "몇 번 왔는지"는 셀 수 있지만 "각 알림에 실린 내용"은 전달하지 못한다. 내용은 락으로 보호한 큐(링버퍼)에 넣고, eventfd는 "큐를 봐"라는 초인종으로만 쓴다 — 펌웨어의 "메일박스 데이터 + doorbell 인터럽트" 패턴 그대로다.

**드라이버·커널 연결**: eventfd는 커널 쪽에서도 신호용으로 쓰인다. KVM의 irqfd/ioeventfd, VFIO의 인터럽트 전달(`VFIO_DEVICE_SET_IRQS`로 eventfd를 등록하면 장치 인터럽트가 그 eventfd를 올린다)이 대표적이다. 유저스페이스 드라이버(VFIO, UIO 비교는 G 모듈)에서 "인터럽트 = eventfd가 읽을 수 있게 됨"이 된다.

---

## 6. timerfd — 타이머를 fd로

### 6.1 정의

```c
int timerfd_create(int clockid, int flags);   /* CLOCK_MONOTONIC 권장 */
int timerfd_settime(int fd, int flags, const struct itimerspec *new, struct itimerspec *old);
```

- `it_value`: 첫 만료까지 시간, `it_interval`: 이후 주기(0이면 한 번만).
- 만료되면 fd가 `POLLIN`이 되고, `read()`는 **8바이트 `uint64_t` = 마지막 read 이후 만료 횟수**를 돌려준다.
- 그 숫자가 1보다 크면 **처리가 늦어서 tick을 놓쳤다(overrun)** 는 뜻이다. 펌웨어로 치면 타이머 ISR에서 "missed tick counter"를 세는 것과 같다.
- `TFD_TIMER_ABSTIME`을 주면 절대 시각 기준. `CLOCK_REALTIME`은 NTP·사용자가 시각을 바꾸면 영향을 받으므로 주기 샘플링에는 `CLOCK_MONOTONIC`을 쓴다(`CLOCK_BOOTTIME`은 suspend 시간도 센다).

왜 `usleep()` 루프 대신 timerfd인가: `while(1){ work(); usleep(10000); }`은 work 시간만큼 주기가 **늘어나고(drift)**, 다른 이벤트와 같이 기다릴 수 없다. timerfd는 커널 hrtimer가 절대 주기로 만료되므로 drift가 없고, epoll에 넣어서 소켓과 같이 기다릴 수 있다.

### 6.2 예제 — 100 ms 샘플링과 overrun

```c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <sys/timerfd.h>

static double now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

int main(void) {
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    if (tfd < 0) { perror("timerfd_create"); exit(1); }
    struct itimerspec its = {
        .it_value    = { .tv_sec = 0, .tv_nsec = 100 * 1000000 },  /* 첫 만료 */
        .it_interval = { .tv_sec = 0, .tv_nsec = 100 * 1000000 },  /* 이후 주기 */
    };
    if (timerfd_settime(tfd, 0, &its, NULL) < 0) { perror("settime"); exit(1); }
    double t0 = now_ms();
    for (int tick = 1; tick <= 6; tick++) {
        uint64_t exp;
        if (read(tfd, &exp, sizeof exp) != sizeof exp) { perror("read"); exit(1); }
        printf("[%7.2f ms] tick %d, expirations=%llu%s\n", now_ms() - t0, tick,
               (unsigned long long)exp, exp > 1 ? "  <-- 샘플을 놓쳤다(overrun)" : "");
        if (tick == 3) usleep(350000);   /* 센서 처리 함수가 350ms 막혔다고 하자 */
    }
    struct itimerspec cur;
    if (timerfd_gettime(tfd, &cur) < 0) { perror("gettime"); exit(1); }
    printf("다음 만료까지 %.2f ms\n", cur.it_value.tv_nsec / 1e6);
    return 0;
}
```

```text
[ 109.33 ms] tick 1, expirations=1
[ 202.59 ms] tick 2, expirations=1
[ 309.47 ms] tick 3, expirations=1
[ 669.63 ms] tick 4, expirations=3  <-- 샘플을 놓쳤다(overrun)
[ 707.18 ms] tick 5, expirations=1
[ 810.06 ms] tick 6, expirations=1
다음 만료까지 89.93 ms
```

출력에서 볼 것:

- tick 3 이후 350 ms 막혔더니, 다음 read에서 `expirations=3`(400·500·600 ms 만료가 합쳐짐). 놓친 샘플 수를 정확히 안다. 로그에 "missed N samples"를 남기거나, 센서 FIFO에서 그만큼 더 읽어 오는 데 쓴다.
- tick 5는 707 ms에 왔다. 주기의 기준은 "처음 설정한 시각"이라서 지연 후에도 700 ms 격자로 **다시 맞춰진다**(drift 없음).
- 각 tick이 정확히 100 ms 배수가 아니라 2~10 ms 늦다. 이 VM(Lima, 가상화된 타이머)과 일반 커널의 스케줄링 지연 때문이다. 실제 보드에서도 PREEMPT_RT가 아니면 수백 us~ms 지터가 있다. 정량 측정은 N3(cyclictest)에서 한다.

---

## 7. signalfd 미리보기 — 시그널도 fd로

시그널 핸들러는 아무 때나 끼어들어서 그 안에서 쓸 수 있는 함수가 극히 제한된다(async-signal-safe, A6). 이벤트 루프에서는 **시그널을 막아(block) 두고 signalfd로 "읽을 수 있는 이벤트"로 바꾸면**, SIGTERM 처리도 평범한 핸들러 함수 안에서 printf·free·로그 쓰기를 마음대로 할 수 있다. systemd가 `stop` 때 보내는 SIGTERM을 받아 센서를 안전하게 끄는 데 쓴다.

```c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include <sys/signalfd.h>

int main(void) {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) { perror("sigprocmask"); exit(1); } /* 기본 동작(종료) 막기 */
    int sfd = signalfd(-1, &mask, SFD_CLOEXEC);
    if (sfd < 0) { perror("signalfd"); exit(1); }
    printf("pid=%d, waiting...\n", getpid());
    fflush(stdout);
    for (;;) {
        struct pollfd p = { .fd = sfd, .events = POLLIN };
        int n = poll(&p, 1, 5000);
        if (n < 0) { perror("poll"); exit(1); }
        if (n == 0) { printf("timeout\n"); break; }
        struct signalfd_siginfo si;
        if (read(sfd, &si, sizeof si) != sizeof si) { perror("read"); exit(1); }
        printf("got signal %d (%s) from pid %u\n",
               si.ssi_signo, strsignal(si.ssi_signo), si.ssi_pid);
        if (si.ssi_signo == SIGTERM) { printf("SIGTERM -> 정리 후 종료\n"); break; }
    }
    return 0;
}
```

```sh
cd /tmp/A4 && { ./ex10 & } ; sleep 0.3; P=$(pgrep -n -x ex10); kill -INT $P; sleep 0.2; kill -TERM $P; echo "shell pid=$$"; wait
```
```text
pid=187587, waiting...
shell pid=187558
got signal 2 (Interrupt) from pid 187558
got signal 15 (Terminated) from pid 187558
SIGTERM -> 정리 후 종료
```

출력에서 볼 것: SIGINT, SIGTERM이 프로세스를 죽이지 않고 **읽을 수 있는 레코드**(`struct signalfd_siginfo`, 128바이트)로 왔고, 보낸 프로세스(셸)의 pid까지 들어 있다. PID 값은 실행마다 다르다. `sigprocmask`를 빼먹으면 signalfd를 만들어도 기본 동작(종료)이 먼저 일어난다. 멀티스레드에서는 모든 스레드가 그 시그널을 block해야 한다(`pthread_sigmask`, 스레드 생성 전에) — 자세한 내용은 A6.

---

## 8. 모두 합치기 — 센서 데몬 이벤트 루프

### 8.1 구조

이제 지금까지의 재료를 하나의 epoll 루프에 넣는다.

- **timerfd** 250 ms: IMU 샘플링 tick (샘플 수 카운트)
- **UNIX datagram socket**: 자식 프로세스가 LiDAR처럼 300 ms마다 패킷 전송
- **eventfd**: worker 스레드가 600 ms 뒤 "캘리브레이션 끝" 알림
- **stdin**: 운영자 명령(`stat`, `quit`)

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="20" y="40" width="220" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="130" y="65" font-size="13" text-anchor="middle">LiDAR 자식 → socket fd 6</text>
<rect x="20" y="95" width="220" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="130" y="120" font-size="13" text-anchor="middle">커널 hrtimer → timerfd 3</text>
<rect x="20" y="150" width="220" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="130" y="175" font-size="13" text-anchor="middle">calib 스레드 → eventfd 5</text>
<rect x="20" y="205" width="220" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="130" y="230" font-size="13" text-anchor="middle">운영자 → stdin fd 0</text>
<line x1="240" y1="60" x2="288" y2="110" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="240" y1="115" x2="288" y2="130" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="240" y1="170" x2="288" y2="150" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="240" y1="225" x2="288" y2="170" stroke="currentColor" marker-end="url(#ah4)"/>
<rect x="290" y="70" width="170" height="140" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="375" y="120" font-size="13" text-anchor="middle">epoll fd 4</text>
<text x="375" y="145" font-size="13" text-anchor="middle">data.u64 = 태그</text>
<text x="375" y="170" font-size="13" text-anchor="middle">(TIMER, LIDAR, ...)</text>
<line x1="460" y1="140" x2="498" y2="140" stroke="currentColor" marker-end="url(#ah4)"/>
<rect x="500" y="60" width="165" height="165" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="515" y="90" font-size="13">while (running)</text>
<text x="515" y="115" font-size="13">  epoll_wait()</text>
<text x="515" y="140" font-size="13">  switch (tag)</text>
<text x="515" y="165" font-size="13">  → 핸들러 실행</text>
<text x="515" y="190" font-size="13">  → 다시 대기</text>
<text x="20" y="280" font-size="13">규칙 1: 핸들러 안에서 절대 block 하지 않는다 (sleep, 느린 파일 쓰기 금지)</text>
<text x="20" y="305" font-size="13">규칙 2: 오래 걸리는 일은 worker 스레드로 보내고, 끝나면 eventfd로 알림 받는다</text>
</svg>
```
그림 4 — 예제 ex9의 구조. 네 종류의 이벤트 소스가 전부 fd가 되어 epoll 하나로 모인다. fd 번호는 실제 실행에서 `/proc/<pid>/fd`로 확인한 값이다.

### 8.2 코드

```c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <sys/socket.h>
#define DIE(m) do { perror(m); exit(1); } while (0)
enum { SRC_TIMER, SRC_LIDAR, SRC_WORKER, SRC_STDIN };
static int ep, efd, samples, pkts, running = 1;
static struct timespec t0;
static double ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (t.tv_sec - t0.tv_sec) * 1e3 + (t.tv_nsec - t0.tv_nsec) / 1e6; }
static void watch(int fd, uint64_t tag) {
    struct epoll_event ev = { .events = EPOLLIN, .data.u64 = tag };
    if (epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev) < 0) DIE("epoll_ctl"); }
static void *calib(void *a) {                       /* worker 스레드: 600ms 뒤 "끝" 알림 */
    uint64_t one = 1; usleep(600000);
    if (write(efd, &one, 8) != 8) DIE("write");
    return a; }

int main(void) {
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int sv[2], tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    if ((ep = epoll_create1(0)) < 0 || tfd < 0 || (efd = eventfd(0, 0)) < 0) DIE("create");
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) < 0) DIE("socketpair");
    if (fork() == 0) {                              /* 자식 = LiDAR 흉내: 300ms마다 패킷 */
        for (int i = 1; i <= 5; i++) { usleep(300000); dprintf(sv[1], "lidar pkt %d", i); }
        _exit(0);
    }
    struct itimerspec its = { {0, 250000000}, {0, 250000000} };   /* 250ms IMU 샘플 */
    if (timerfd_settime(tfd, 0, &its, NULL) < 0) DIE("settime");
    watch(tfd, SRC_TIMER); watch(sv[0], SRC_LIDAR); watch(efd, SRC_WORKER); watch(0, SRC_STDIN);
    pthread_t th; pthread_create(&th, NULL, calib, NULL);
    while (running) {
        struct epoll_event evs[8];
        int n = epoll_wait(ep, evs, 8, -1);
        if (n < 0) DIE("epoll_wait");
        for (int i = 0; i < n; i++) {
            char buf[64]; uint64_t v; ssize_t r;
            switch (evs[i].data.u64) {
            case SRC_TIMER:  if (read(tfd, &v, 8) == 8) samples += v; break;
            case SRC_LIDAR:  r = recv(sv[0], buf, sizeof buf - 1, 0); buf[r > 0 ? r : 0] = 0;
                             pkts++; printf("[%6.1f] %s\n", ms(), buf); break;
            case SRC_WORKER: if (read(efd, &v, 8) == 8) printf("[%6.1f] worker: calibration done\n", ms()); break;
            case SRC_STDIN:  r = read(0, buf, sizeof buf - 1);
                             if (r <= 0 || !strncmp(buf, "quit", 4)) running = 0;
                             else printf("[%6.1f] stat: imu samples=%d lidar pkts=%d\n", ms(), samples, pkts);
                             break;
            }
        }
    }
    printf("[%6.1f] quit: samples=%d pkts=%d\n", ms(), samples, pkts);
    pthread_join(th, NULL); return 0;
}
```

stdin에 1초 뒤 `stat`, 다시 0.7초 뒤 `quit`을 넣어 실행한다.

```sh
limactl shell elinux -- bash -lc 'gcc -std=gnu11 -Wall -Wextra -O2 -pthread /Users/donh/.elinux_scratch/A4/ex9_event_loop.c -o /tmp/A4/ex9 && cd /tmp/A4 && (sleep 1.0; echo stat; sleep 0.7; echo quit) | ./ex9'
```
```text
[ 309.3] lidar pkt 1
[ 608.2] worker: calibration done
[ 609.6] lidar pkt 2
[ 910.2] lidar pkt 3
[1007.0] stat: imu samples=3 lidar pkts=3
[1218.3] lidar pkt 4
[1525.0] lidar pkt 5
[1710.7] quit: samples=6 pkts=5
```

출력에서 볼 것: 네 종류 이벤트가 섞여 들어오는데 스레드 하나가 순서대로 처리했다. 1007 ms의 `stat`에서 IMU 샘플이 3개(250·500·750 ms tick, 1000 ms tick은 아직 처리 전), 1710 ms 종료 시 6개(1500 ms까지). 락은 하나도 없다 — `samples`, `pkts`를 건드리는 것이 루프 스레드뿐이기 때문이다.

> 이 예제는 짧게 쓰려고 몇 가지를 생략했다: 종료한 자식을 `waitpid`로 거두지 않고(좀비, A2), `recv` 실패를 세밀하게 처리하지 않는다. 실제 데몬에서는 SIGTERM을 §7의 signalfd로 같은 루프에 넣고, 자식 종료도 signalfd(SIGCHLD)나 pidfd로 받는다.

### 8.3 /proc으로 들여다보기 — 실행 중인 루프의 fd

루프가 도는 동안 다른 셸에서 fd 테이블과 각 fd의 커널 상태를 본다.

```sh
cd /tmp/A4 && ( (sleep 2.5; echo quit) | ./ex9 > /tmp/A4/ex9.out & ) ; sleep 0.5
P=$(pgrep -o -x ex9); echo PID=$P
ls -l /proc/$P/fd | tail -n +2 | awk '{print $9, $10, $11}'
cat /proc/$P/fdinfo/4
```
```text
PID=186397
0 -> pipe:[120630]
1 -> /tmp/A4/ex9.out
2 -> pipe:[121761]
3 -> anon_inode:[timerfd]
4 -> anon_inode:[eventpoll]
5 -> anon_inode:[eventfd]
6 -> socket:[120631]
7 -> socket:[120632]
pos:	0
flags:	02
mnt_id:	19
ino:	1077
tfd:        0 events:       19 data:                3  pos:0 ino:1d736 sdev:10
tfd:        6 events:       19 data:                1  pos:0 ino:1d737 sdev:a
tfd:        5 events:       19 data:                2  pos:0 ino:435 sdev:11
tfd:        3 events:       19 data:                0  pos:0 ino:435 sdev:11
```

출력에서 볼 것: timerfd·epoll·eventfd는 디스크 파일이 아니라 **`anon_inode`**(디스크에 없는 익명 inode에 커널 객체를 붙인 특수 파일)다. epoll의 fdinfo에서 `data:` 열이 우리가 넣은 태그(0=TIMER, 1=LIDAR, 2=WORKER, 3=STDIN)다. PID·inode 번호는 실행마다 다르다.

timerfd와 eventfd의 fdinfo도 커널 상태를 그대로 보여 준다(다른 실행에서 0.8초 시점에 찍음).

```text
--- /proc/$PID/fdinfo/3
pos:	0
flags:	02000002
mnt_id:	19
ino:	1077
clockid: 1
ticks: 0
settime flags: 00
it_value: (0, 193155256)
it_interval: (0, 250000000)
--- /proc/$PID/fdinfo/5
pos:	0
flags:	02
mnt_id:	19
ino:	1077
eventfd-count:                0
eventfd-id: 28
eventfd-semaphore: 0
```

`clockid: 1`은 `CLOCK_MONOTONIC`, `it_value`는 다음 만료까지 남은 시간(약 193 ms), `ticks: 0`은 아직 read하지 않은 만료 횟수다. 현장에서 "타이머가 안 도는 것 같다"는 의심이 들면 이 파일부터 본다.

### 8.4 strace로 보는 이벤트 루프

```sh
cd /tmp/A4 && (sleep 0.7; echo quit) | strace -e trace=epoll_create1,epoll_ctl,epoll_wait,epoll_pwait,timerfd_create,timerfd_settime,eventfd2,read,recvfrom -o ex9.strace ./ex9 >/dev/null
```
```text
timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC) = 3
epoll_create1(0)                        = 4
eventfd2(0, 0)                          = 5
timerfd_settime(3, 0, {it_interval={tv_sec=0, tv_nsec=250000000}, it_value={tv_sec=0, tv_nsec=250000000}}, NULL) = 0
epoll_ctl(4, EPOLL_CTL_ADD, 3, {events=EPOLLIN, data=0}) = 0
epoll_ctl(4, EPOLL_CTL_ADD, 6, {events=EPOLLIN, data=0x1}) = 0
epoll_ctl(4, EPOLL_CTL_ADD, 5, {events=EPOLLIN, data=0x2}) = 0
epoll_ctl(4, EPOLL_CTL_ADD, 0, {events=EPOLLIN, data=0x3}) = 0
...
epoll_pwait(4, [{events=EPOLLIN, data=0}], 8, -1, NULL, 8) = 1
read(3, "\1\0\0\0\0\0\0\0", 8)          = 8
epoll_pwait(4, [{events=EPOLLIN, data=0x1}], 8, -1, NULL, 8) = 1
recvfrom(6, "lidar pkt 1", 63, 0, NULL, NULL) = 11
epoll_pwait(4, [{events=EPOLLIN, data=0}], 8, -1, NULL, 8) = 1
read(3, "\1\0\0\0\0\0\0\0", 8)          = 8
epoll_pwait(4, [{events=EPOLLIN, data=0x2}], 8, -1, NULL, 8) = 1
read(5, "\1\0\0\0\0\0\0\0", 8)          = 8
epoll_pwait(4, [{events=EPOLLIN, data=0x1}], 8, -1, NULL, 8) = 1
recvfrom(6, "lidar pkt 2", 63, 0, NULL, NULL) = 11
epoll_pwait(4, [{events=EPOLLIN, data=0x3}], 8, -1, NULL, 8) = 1
read(0, "quit\n", 63)                   = 5
```

(두 실행 결과를 이어 붙였다. 위 8줄은 셋업, `...` 아래는 루프 부분이다. 동적 로더의 `read` 줄 등은 생략했다.)

출력에서 볼 것: 셋업 때 `epoll_ctl`이 4번, 그 뒤로는 **"`epoll_pwait` 1번 → 해당 fd `read` 1번"** 패턴만 반복된다. 이벤트 루프의 이상적인 syscall 모양이다. 그리고 §2.3에서 말한 대로 aarch64에서는 `epoll_wait`가 `epoll_pwait`로 보인다. `recv`도 `recvfrom`으로 보인다. 처음엔 `epoll_wait`만 필터에 넣었다가 아무것도 안 잡혔다 — 보드에서 strace할 때 그대로 겪을 함정이다.

---

## 9. 확장성 측정 — select vs poll vs epoll

### 9.1 실험 설계

N개의 pipe를 만들어 모두 감시하되, **마지막 하나만** 계속 활성으로 만든다(데이터 1바이트 쓰기 → 대기 → 1바이트 읽기). "센서·클라이언트는 많은데 대부분 조용하다"는 현실적인 상황이다. 한 번 반복(write + 대기 syscall + read)에 걸린 평균 시간을 잰다. select는 fd 번호가 1024를 넘으면 쓸 수 없으므로 그 구간은 `-1`로 표시한다.

```c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/epoll.h>

static double now_us(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

int main(int argc, char **argv) {
    int N = argc > 1 ? atoi(argv[1]) : 100, only = argc > 2 ? atoi(argv[2]) : -1;
    int *rfd = malloc(sizeof(int) * N), *wfd = malloc(sizeof(int) * N), maxfd = 0;
    struct pollfd *pfd = calloc(N, sizeof *pfd);
    int ep = epoll_create1(0);
    if (!rfd || !wfd || !pfd || ep < 0) { perror("init"); exit(1); }
    for (int i = 0; i < N; i++) {
        int p[2];
        if (pipe(p) < 0) { perror("pipe (ulimit -n?)"); exit(1); }
        rfd[i] = p[0]; wfd[i] = p[1]; if (p[0] > maxfd) maxfd = p[0];
        pfd[i].fd = p[0]; pfd[i].events = POLLIN;
        struct epoll_event ev = { .events = EPOLLIN, .data.fd = p[0] };
        if (epoll_ctl(ep, EPOLL_CTL_ADD, p[0], &ev) < 0) { perror("epoll_ctl"); exit(1); }
    }
    int iters = 2000000 / N; if (iters < 300) iters = 300; if (iters > 20000) iters = 20000;
    int act = N - 1; char c = 'x';
    double res[3] = { -1, -1, -1 };
    for (int m = 0; m < 3; m++) {
        if ((m == 0 && maxfd >= FD_SETSIZE) || (only >= 0 && m != only)) continue; /* select: fd<1024만 */
        double t0 = now_us();
        for (int k = 0; k < iters; k++) {
            if (write(wfd[act], &c, 1) != 1) exit(1);
            int n;
            if (m == 0) {
                fd_set s; FD_ZERO(&s);
                for (int i = 0; i < N; i++) FD_SET(rfd[i], &s);   /* 매번 N개 다시 등록 */
                n = select(maxfd + 1, &s, NULL, NULL, NULL);
            } else if (m == 1) {
                n = poll(pfd, N, -1);
            } else {
                struct epoll_event out[4];
                n = epoll_wait(ep, out, 4, -1);
            }
            if (n != 1 || read(rfd[act], &c, 1) != 1) { perror("wait"); exit(1); }
        }
        res[m] = (now_us() - t0) / iters;
    }
    printf("%5d %10.2f %10.2f %10.2f\n", N, res[0], res[1], res[2]);
    return 0;
}
```

N=5000이면 pipe 하나에 fd 2개라 fd가 10000개 넘게 필요하다. 기본 `ulimit -n`은 1024라서 먼저 올린다(하드 리밋 524288 이내).

```sh
limactl shell elinux -- bash -lc 'ulimit -n; ulimit -Hn'
```
```text
1024
524288
```

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A4 && ulimit -n 20000 && for r in 1 2 3; do echo "run $r"; for n in 10 100 250 500 1000 2000 5000; do ./ex11 $n; done; done'
```
```text
run 1
   10       1.07       0.99       0.59
  100       4.35       4.03       0.75
  250      10.51       9.88       0.57
  500      21.84      20.73       0.56
 1000      -1.00      41.57       0.56
 2000      -1.00      83.25       0.57
 5000      -1.00     222.54       0.56
run 2
   10       1.04       0.96       0.57
  100       4.30       4.11       0.59
  250      10.48       9.84       0.56
  500      22.93      21.82       0.57
 1000      -1.00      42.36       0.56
 2000      -1.00      83.76       0.56
 5000      -1.00     213.03       0.56
run 3
   10       1.20       1.14       0.58
  100       4.31       3.90       0.56
  250      10.55       9.89       0.57
  500      21.55      20.70       0.56
 1000      -1.00      41.82       0.57
 2000      -1.00      84.51       0.59
 5000      -1.00     228.71       0.59
```

열은 왼쪽부터 `N`, select, poll, epoll이고 단위는 us/반복이다. 세 번 반복해도 5% 안쪽으로 일정하다.

```svg
<svg viewBox="0 0 640 400" xmlns="http://www.w3.org/2000/svg">
<line x1="80" y1="330" x2="600" y2="330" stroke="currentColor"/>
<line x1="80" y1="40" x2="80" y2="330" stroke="currentColor"/>
<line x1="80" y1="309.4" x2="600" y2="309.4" stroke="#888" stroke-opacity="0.3"/>
<text x="74" y="313.4" font-size="12" text-anchor="end">0.5</text>
<line x1="80" y1="281.5" x2="600" y2="281.5" stroke="#888" stroke-opacity="0.3"/>
<text x="74" y="285.5" font-size="12" text-anchor="end">1</text>
<line x1="80" y1="253.5" x2="600" y2="253.5" stroke="#888" stroke-opacity="0.3"/>
<text x="74" y="257.5" font-size="12" text-anchor="end">2</text>
<line x1="80" y1="216.6" x2="600" y2="216.6" stroke="#888" stroke-opacity="0.3"/>
<text x="74" y="220.6" font-size="12" text-anchor="end">5</text>
<line x1="80" y1="188.7" x2="600" y2="188.7" stroke="#888" stroke-opacity="0.3"/>
<text x="74" y="192.7" font-size="12" text-anchor="end">10</text>
<line x1="80" y1="160.7" x2="600" y2="160.7" stroke="#888" stroke-opacity="0.3"/>
<text x="74" y="164.7" font-size="12" text-anchor="end">20</text>
<line x1="80" y1="123.8" x2="600" y2="123.8" stroke="#888" stroke-opacity="0.3"/>
<text x="74" y="127.8" font-size="12" text-anchor="end">50</text>
<line x1="80" y1="95.9" x2="600" y2="95.9" stroke="#888" stroke-opacity="0.3"/>
<text x="74" y="99.9" font-size="12" text-anchor="end">100</text>
<line x1="80" y1="67.9" x2="600" y2="67.9" stroke="#888" stroke-opacity="0.3"/>
<text x="74" y="71.9" font-size="12" text-anchor="end">200</text>
<line x1="127.4" y1="330" x2="127.4" y2="335" stroke="currentColor"/>
<text x="127.4" y="349" font-size="12" text-anchor="middle">10</text>
<line x1="284.9" y1="330" x2="284.9" y2="335" stroke="currentColor"/>
<text x="284.9" y="349" font-size="12" text-anchor="middle">100</text>
<line x1="347.6" y1="330" x2="347.6" y2="335" stroke="currentColor"/>
<text x="347.6" y="349" font-size="12" text-anchor="middle">250</text>
<line x1="395.1" y1="330" x2="395.1" y2="335" stroke="currentColor"/>
<text x="395.1" y="349" font-size="12" text-anchor="middle">500</text>
<line x1="442.5" y1="330" x2="442.5" y2="335" stroke="currentColor"/>
<text x="442.5" y="349" font-size="12" text-anchor="middle">1000</text>
<line x1="489.9" y1="330" x2="489.9" y2="335" stroke="currentColor"/>
<text x="489.9" y="349" font-size="12" text-anchor="middle">2000</text>
<line x1="552.6" y1="330" x2="552.6" y2="335" stroke="currentColor"/>
<text x="552.6" y="349" font-size="12" text-anchor="middle">5000</text>
<text x="340.0" y="370" font-size="13" text-anchor="middle">감시하는 fd 개수 N (log scale, 그중 1개만 활성)</text>
<text x="18" y="185.0" font-size="13" text-anchor="middle" transform="rotate(-90 18 185.0)">한 번 깨어나는 비용 (us, log)</text>
<line x1="396.4" y1="40" x2="396.4" y2="330" stroke="#d0564a" stroke-dasharray="4 3"/>
<text x="401.4" y="230" font-size="12">FD_SETSIZE(1024) 벽</text>
<polyline fill="none" stroke="#e08a3c" stroke-width="2" points="127.4,278.7 284.9,222.2 347.6,186.7 395.1,157.2"/>
<circle cx="127.4" cy="278.7" r="3.5" fill="#e08a3c"/>
<circle cx="284.9" cy="222.2" r="3.5" fill="#e08a3c"/>
<circle cx="347.6" cy="186.7" r="3.5" fill="#e08a3c"/>
<circle cx="395.1" cy="157.2" r="3.5" fill="#e08a3c"/>
<polyline fill="none" stroke="#4a7bd0" stroke-width="2" points="127.4,281.9 284.9,225.3 347.6,189.2 395.1,159.3 442.5,131.3 489.9,103.3 552.6,63.6"/>
<circle cx="127.4" cy="281.9" r="3.5" fill="#4a7bd0"/>
<circle cx="284.9" cy="225.3" r="3.5" fill="#4a7bd0"/>
<circle cx="347.6" cy="189.2" r="3.5" fill="#4a7bd0"/>
<circle cx="395.1" cy="159.3" r="3.5" fill="#4a7bd0"/>
<circle cx="442.5" cy="131.3" r="3.5" fill="#4a7bd0"/>
<circle cx="489.9" cy="103.3" r="3.5" fill="#4a7bd0"/>
<circle cx="552.6" cy="63.6" r="3.5" fill="#4a7bd0"/>
<polyline fill="none" stroke="#3f9a6b" stroke-width="2" points="127.4,302.7 284.9,293.1 347.6,304.1 395.1,304.8 442.5,304.8 489.9,304.1 552.6,304.8"/>
<circle cx="127.4" cy="302.7" r="3.5" fill="#3f9a6b"/>
<circle cx="284.9" cy="293.1" r="3.5" fill="#3f9a6b"/>
<circle cx="347.6" cy="304.1" r="3.5" fill="#3f9a6b"/>
<circle cx="395.1" cy="304.8" r="3.5" fill="#3f9a6b"/>
<circle cx="442.5" cy="304.8" r="3.5" fill="#3f9a6b"/>
<circle cx="489.9" cy="304.1" r="3.5" fill="#3f9a6b"/>
<circle cx="552.6" cy="304.8" r="3.5" fill="#3f9a6b"/>
<text x="544.6" y="55.6" font-size="12" text-anchor="end">poll 222.5 us</text>
<text x="387.1" y="145.2" font-size="12" text-anchor="end">select 21.8 us</text>
<text x="544.6" y="294.8" font-size="12" text-anchor="end">epoll ~0.56 us (N과 무관)</text>
<line x1="100" y1="52" x2="122" y2="52" stroke="#e08a3c" stroke-width="2"/>
<text x="128" y="56" font-size="12">select</text>
<line x1="100" y1="70" x2="122" y2="70" stroke="#4a7bd0" stroke-width="2"/>
<text x="128" y="74" font-size="12">poll</text>
<line x1="100" y1="88" x2="122" y2="88" stroke="#3f9a6b" stroke-width="2"/>
<text x="128" y="92" font-size="12">epoll</text>
</svg>
```
그림 5 — run 1 값을 log-log로 그린 것. select와 poll은 N에 비례(기울기 1)해서 N=5000에서 poll 한 번에 222 us, epoll은 N과 상관없이 약 0.56 us다. 빨간 점선은 select가 쓸 수 없어지는 지점(fd 번호 1024).

출력에서 볼 것:

- **N=10에서도 epoll이 약 2배 빠르지만 절대값은 1 us 남짓** 차이다. fd 몇 개짜리 센서 데몬이라면 poll로 충분하다. epoll의 진짜 장점은 N이 클 때다.
- poll은 N이 2배가 되면 시간도 정확히 약 2배(41.6 → 83.3 us). 1 fd당 약 40 ns — 매 호출 N개 `.poll` 호출 + wait queue 등록·해제 비용이다(§10에서 bpftrace로 센다).
- select는 poll과 거의 같고, 유저가 매번 `FD_SET` N번을 하는 비용이 더해져 조금 더 느리다. N=500(가장 큰 read fd 1002)이 이 실험에서 쓸 수 있는 한계였다.
- 측정 환경은 Apple Silicon Mac 위 Lima VM(aarch64, 4 vCPU)이라 절대값은 실제 보드와 다르다. **비율과 기울기**를 기억한다.

### 9.2 그럼 epoll이 항상 정답인가?

| 상황 | 추천 |
|---|---|
| fd 몇 개, 이식성 필요 | poll |
| fd 수백~수천 (서버, 많은 센서·소켓) | epoll |
| fd 집합이 매번 바뀜 (짧은 연결이 계속 생겼다 사라짐) | epoll이지만 `epoll_ctl` syscall 비용도 고려 |
| 일반 파일(디스크) 비동기 I/O | epoll 불가 → 스레드 풀 또는 io_uring |
| 아주 짧은 지연 (us 단위 루프) | busy polling(non-blocking read 반복, CPU 하나 전용) |

### 9.3 그 다음: io_uring 한 문단

epoll은 "**준비됐다고 알려 주면 내가 read 한다**"(readiness 모델)이다. io_uring(Linux 5.1+)은 "**read를 미리 맡겨 두면 끝났을 때 결과를 준다**"(completion 모델)이다. 유저와 커널이 공유하는 링버퍼 두 개(submission queue, completion queue)로 요청과 완료를 주고받아 syscall 횟수를 크게 줄이고, 일반 파일 I/O도 진짜 비동기로 할 수 있다. Don에게는 **NVMe의 SQ/CQ 구조와 같은 모양**이라고 보면 바로 이해될 것이다. 다만 보안 이슈로 일부 배포판·컨테이너 환경에서 비활성화되기도 하고, 임베디드 센서 데몬에서는 아직 epoll이 표준이다. 이 노트에서는 이름과 모델만 기억한다.

---

## 10. 커널 안에서는 무슨 일이 일어나나

### 10.1 공통 기반: 모든 것은 드라이버의 `.poll`에서 시작한다

select, poll, epoll 모두 결국 각 fd의 `struct file`을 찾아 `file->f_op->poll(file, poll_table)`을 호출한다(`vfs_poll()`). 이 `.poll` 메서드는 **드라이버(또는 pipe·socket·timerfd 구현)가 제공하는 함수**이고, 하는 일은 딱 두 가지다.

1. `poll_wait(filp, &dev->wq, wait)`: "이 장치의 상태가 바뀌면 깨워 달라"고 **자기 wait queue를 poll_table에 알려 준다**. 이 함수 자체는 잠들지 않는다. poll_table에 들어 있는 콜백(`_qproc`)을 불러서 wait queue에 엔트리를 하나 거는 것뿐이다.
2. **지금 상태**를 mask로 계산해 리턴한다(`EPOLLIN | EPOLLRDNORM` 등).

센서 드라이버라면 이렇게 생겼다(G6 미리보기 — 실행하지 않은 커널 코드 스케치다).

```c
/* 드라이버 쪽 (커널 코드, G4/G6에서 직접 빌드한다) */
static __poll_t imu_poll(struct file *filp, struct poll_table_struct *wait)
{
    struct imu_dev *dev = filp->private_data;
    __poll_t mask = 0;

    poll_wait(filp, &dev->read_wq, wait);       /* 1) 내 wait queue를 등록 */
    if (!kfifo_is_empty(&dev->fifo))
        mask |= EPOLLIN | EPOLLRDNORM;          /* 2) 지금 읽을 게 있나 */
    return mask;
}

static irqreturn_t imu_irq_thread(int irq, void *data)
{
    struct imu_dev *dev = data;
    /* ... I2C/SPI로 샘플을 읽어 kfifo에 넣는다 ... */
    wake_up_interruptible(&dev->read_wq);       /* 3) 기다리는 사람 깨우기 */
    return IRQ_HANDLED;
}

static const struct file_operations imu_fops = {
    .owner = THIS_MODULE,
    .read  = imu_read,
    .poll  = imu_poll,
};
```

**`poll_wait`가 거는 엔트리의 종류**가 select/poll과 epoll의 차이를 만든다.

| | select / poll | epoll |
|---|---|---|
| poll_table의 `_qproc` | `__pollwait` | `ep_ptable_queue_proc` |
| wait queue에 거는 엔트리 | `poll_table_entry` (호출마다 새로 할당) | `eppoll_entry` (`EPOLL_CTL_ADD` 때 한 번) |
| 깨울 때 불리는 함수 | `pollwake` → 잠든 task를 깨움 | `ep_poll_callback` → epitem을 ready list에 넣음 |
| 엔트리 수명 | syscall 끝나면 전부 제거 | `EPOLL_CTL_DEL` 또는 파일이 닫힐 때까지 |
| 깨어난 뒤 | N개 fd의 `.poll`을 **다시 전부** 호출 | ready list에 있는 것만 `.poll`로 mask 재확인 |

### 10.2 poll vs epoll 구조

```svg
<svg viewBox="0 0 680 410" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="24" font-size="14">poll(fds, N): 호출할 때마다 전부</text>
<rect x="20" y="40" width="300" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="32" y="62" font-size="13">1. pollfd[N] 복사 (user → kernel)</text>
<rect x="20" y="88" width="300" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="32" y="110" font-size="13">2. N개 모두 .poll() + __pollwait</text>
<rect x="20" y="136" width="300" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="32" y="158" font-size="13">3. 잠듦 (N개 wait queue에 엔트리)</text>
<rect x="20" y="184" width="300" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="32" y="206" font-size="13">4. 깨면 다시 N개 모두 .poll()</text>
<rect x="20" y="232" width="300" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="32" y="254" font-size="13">5. N개 엔트리 제거 + revents 복사</text>
<line x1="170" y1="74" x2="170" y2="86" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="170" y1="122" x2="170" y2="134" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="170" y1="170" x2="170" y2="182" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="170" y1="218" x2="170" y2="230" stroke="currentColor" marker-end="url(#ah5)"/>
<text x="20" y="296" font-size="13">비용 ∝ N (매 호출마다)</text>
<line x1="340" y1="10" x2="340" y2="400" stroke="#888" stroke-dasharray="4 4"/>
<text x="360" y="24" font-size="14">epoll: 등록 1번, 대기는 ready list만</text>
<rect x="360" y="40" width="305" height="120" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="372" y="62" font-size="13">struct eventpoll (epoll fd 1개)</text>
<text x="372" y="88" font-size="13">rbr: epitem rb-tree (관심 목록)</text>
<text x="372" y="112" font-size="13">rdllist: ready list</text>
<text x="372" y="136" font-size="13">wq: epoll_wait에서 자는 task</text>
<rect x="360" y="200" width="95" height="60" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="407" y="225" font-size="13" text-anchor="middle">file A</text>
<text x="407" y="245" font-size="12" text-anchor="middle">wait queue</text>
<rect x="465" y="200" width="95" height="60" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="512" y="225" font-size="13" text-anchor="middle">file B</text>
<text x="512" y="245" font-size="12" text-anchor="middle">wait queue</text>
<rect x="570" y="200" width="95" height="60" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="617" y="225" font-size="13" text-anchor="middle">file C</text>
<text x="617" y="245" font-size="12" text-anchor="middle">wait queue</text>
<line x1="512" y1="198" x2="470" y2="118" stroke="#d0564a" stroke-width="2" marker-end="url(#ah5)"/>
<text x="360" y="290" font-size="13">각 file에 eppoll_entry를 ADD 때 한 번 건다</text>
<text x="360" y="312" font-size="13">B에 데이터 → ep_poll_callback()</text>
<text x="360" y="334" font-size="13">→ B의 epitem을 rdllist에 추가 → wq 깨움</text>
<text x="360" y="366" font-size="13">epoll_wait 비용 ∝ 준비된 fd 개수</text>
</svg>
```
그림 6 — 왼쪽: poll은 매 호출마다 N개 전부를 등록·검사·해제한다. 오른쪽: epoll은 관심 목록(rb-tree)과 콜백을 미리 걸어 두고, 이벤트가 생긴 fd만 콜백이 ready list에 넣는다.

### 10.3 깨우기 경로 — IRQ에서 epoll_wait 리턴까지

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="20" y="30" width="180" height="36" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="110" y="53" font-size="13" text-anchor="middle">IRQ: 새 샘플 도착</text>
<line x1="200" y1="48" x2="233" y2="48" stroke="currentColor" marker-end="url(#ah6)"/>
<rect x="235" y="30" width="210" height="36" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="53" font-size="13" text-anchor="middle">handler: fifo에 저장</text>
<line x1="445" y1="48" x2="468" y2="48" stroke="currentColor" marker-end="url(#ah6)"/>
<rect x="470" y="30" width="195" height="36" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="567" y="53" font-size="13" text-anchor="middle">wake_up(&amp;dev->wq)</text>
<line x1="567" y1="66" x2="480" y2="104" stroke="currentColor" marker-end="url(#ah6)"/>
<rect x="130" y="106" width="430" height="36" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="345" y="129" font-size="13" text-anchor="middle">dev->wq: wait queue (걸린 엔트리를 차례로 호출)</text>
<line x1="240" y1="142" x2="175" y2="174" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="450" y1="142" x2="510" y2="174" stroke="currentColor" marker-end="url(#ah6)"/>
<rect x="20" y="176" width="310" height="36" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="175" y="199" font-size="13" text-anchor="middle">poll/select 엔트리: pollwake()</text>
<rect x="350" y="176" width="315" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="507" y="199" font-size="13" text-anchor="middle">epoll 엔트리: ep_poll_callback()</text>
<line x1="175" y1="212" x2="175" y2="234" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="507" y1="212" x2="507" y2="234" stroke="currentColor" marker-end="url(#ah6)"/>
<rect x="20" y="236" width="310" height="36" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="175" y="259" font-size="13" text-anchor="middle">task 깨움 → do_poll이 N개 .poll 재호출</text>
<rect x="350" y="236" width="315" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="507" y="259" font-size="13" text-anchor="middle">epitem → ep->rdllist 추가, ep->wq 깨움</text>
<line x1="507" y1="272" x2="507" y2="294" stroke="currentColor" marker-end="url(#ah6)"/>
<rect x="350" y="296" width="315" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="507" y="319" font-size="13" text-anchor="middle">epoll_wait: ready fd만 재확인 → 복사</text>
<rect x="20" y="296" width="310" height="56" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="175" y="319" font-size="13" text-anchor="middle">등록 시점: 드라이버 .poll 안의</text>
<text x="175" y="341" font-size="13" text-anchor="middle">poll_wait()가 엔트리를 dev->wq에 건다</text>
<text x="20" y="384" font-size="13">같은 wait queue에 poll 엔트리와 epoll 엔트리가 함께 걸려 있을 수 있다</text>
</svg>
```
그림 7 — 드라이버는 "누가 기다리는지" 모른다. wait queue를 깨우기만 하면, 걸려 있는 엔트리의 콜백(pollwake 또는 ep_poll_callback)이 각자 알아서 처리한다.

조금 더 정확히, 현재 커널(`fs/eventpoll.c`, 6.x~7.0) 기준의 epoll 경로:

1. `epoll_ctl(ADD)` → `ep_insert()`: `struct epitem`을 만들어 `ep->rbr`(rb-tree)에 넣고, `ep_item_poll()`로 대상 파일의 `.poll`을 한 번 호출한다. 이때 poll_table의 `_qproc`가 `ep_ptable_queue_proc`라서, 드라이버의 `poll_wait()`가 `eppoll_entry`(wait 함수 = `ep_poll_callback`)를 드라이버 wait queue에 건다. 이미 준비 상태면 바로 ready list에 넣는다.
2. 드라이버가 `wake_up(&dev->wq)` → wait queue의 각 엔트리 함수 호출 → `ep_poll_callback()`: 해당 epitem을 `ep->rdllist`에 추가(이미 있으면 그대로)하고 `ep->wq`에서 자는 `epoll_wait` 호출자를 깨운다. **콜백은 wake_up 문맥(인터럽트 문맥일 수도 있다)에서 돌기 때문에 잠들지 않는 짧은 일만** 한다(F2).
3. `epoll_wait` → `do_epoll_wait()` → `ep_poll()`: rdllist가 비어 있으면 잠들고, 깨면 `ep_send_events()`가 rdllist의 각 epitem에 대해 `.poll`을 `_qproc` 없이 다시 불러 **현재 mask**를 확인하고 유저 배열에 복사한다.
4. 여기서 LT/ET/ONESHOT 차이가 생긴다: **LT면 epitem을 rdllist 뒤에 다시 넣는다**(다음 `epoll_wait`에서 다시 `.poll`로 확인 → 데이터가 남아 있으면 또 보고). **ET면 다시 넣지 않는다**(다음 `ep_poll_callback`, 즉 다음 wake_up이 와야 다시 들어감). **ONESHOT이면 이벤트 mask를 지워** 콜백이 와도 무시하게 만든다. §4.4~4.5의 실험 결과가 바로 이 코드 몇 줄에서 나온다.

### 10.4 bpftrace로 직접 세어 보기

정말 poll은 매번 N개의 `.poll`을 부르고 epoll은 안 부르는지, 커널 함수 호출 횟수를 센다. ex11을 N=1000으로 poll 모드(1)와 epoll 모드(2)만 각각 돌리면서(반복 2000회) kprobe로 센다. 먼저 함수들이 트레이스 가능한지 확인한다.

```sh
for f in pipe_poll ep_poll_callback ep_item_poll do_sys_poll __pollwait do_epoll_wait ep_poll ep_ptable_queue_proc; do sudo grep -w "$f" /sys/kernel/tracing/available_filter_functions || echo "$f: not traceable"; done
```
```text
pipe_poll
ep_poll_callback
ep_item_poll.isra.0
do_sys_poll
__pollwait
do_epoll_wait
ep_poll
ep_ptable_queue_proc
```

(`ep_item_poll.isra.0`은 컴파일러 최적화로 이름이 바뀐 것이다.) 이제 센다.

```sh
cd /tmp/A4 && ulimit -n 20000 && for m in 1 2; do echo "=== ./ex11 1000 $m ==="; sudo bpftrace -q -e "kprobe:pipe_poll,kprobe:__pollwait,kprobe:ep_poll_callback,kprobe:ep_ptable_queue_proc,kprobe:do_sys_poll,kprobe:do_epoll_wait /comm == \"ex11\"/ { @[func] = count(); }" -c "/tmp/A4/ex11 1000 $m"; done
```
```text
=== ./ex11 1000 1 ===
 1000      -1.00     282.64      -1.00
@[ep_ptable_queue_proc]: 1000
@[do_sys_poll]: 2000
@[ep_poll_callback]: 3000
@[__pollwait]: 2000000
@[pipe_poll]: 2001000
=== ./ex11 1000 2 ===
 1000      -1.00      -1.00       1.14
@[ep_ptable_queue_proc]: 1000
@[do_epoll_wait]: 2000
@[pipe_poll]: 3000
@[ep_poll_callback]: 3000
```

출력에서 볼 것(빈 줄은 지웠다):

- **poll 모드**: `do_sys_poll` 2000번(= poll 호출 수) 동안 `pipe_poll`이 2,001,000번, `__pollwait`가 2,000,000번. 즉 **poll 1번 = pipe `.poll` 1000번 + wait queue 등록 1000번**이다. 남는 1000번은 셋업 때 `epoll_ctl(ADD)`가 부른 것이다.
- **epoll 모드**: `do_epoll_wait` 2000번 동안 `pipe_poll`은 3000번 = ADD 1000번 + **wait 1번당 1번**(ready list에 있는 그 fd만). N과 무관하다.
- `ep_ptable_queue_proc` 1000 = ADD 때 한 번씩 건 `eppoll_entry`. 두 모드 모두에 보이는 것은 이 프로그램이 셋업에서 모든 pipe를 epoll에도 등록했기 때문이다. 같은 이유로 poll 모드에서도 write가 wake_up을 할 때 `ep_poll_callback`이 불린다. 횟수(3000)는 write 횟수(2000)보다 조금 많은데, pipe 구현이 epoll 사용자를 위해 추가 wakeup을 하는 등 세부 구현에 따른 것이라 여기서는 "N에 비례하지 않는다"는 점만 본다.
- bpftrace 프로브를 붙인 상태라 시간(282 us, 1.14 us)은 §9보다 크다. 프로브 오버헤드다.

### 10.5 eventfd·timerfd·signalfd는 커널에서 무엇인가

셋 다 `anon_inode` 파일에 자기 `file_operations`(`.read`, `.write`, `.poll`)를 붙인 것이다. 구조가 드라이버와 **완전히 같다**.

| fd 종류 | 커널 쪽 상태 | `.poll`이 보는 것 | 누가 `wake_up` 하나 |
|---|---|---|---|
| eventfd | `struct eventfd_ctx` (count, wqh) | `count > 0` | `write()` 또는 커널 내부의 `eventfd_signal()` |
| timerfd | hrtimer + 만료 카운트(`ticks`) | `ticks > 0` | hrtimer 콜백(`timerfd_tmrproc`) |
| signalfd | 태스크의 pending 시그널 | 마스크된 시그널이 pending인가 | 시그널 전달 경로 |
| 장치 드라이버 | 드라이버 private (`kfifo` 등) | 드라이버가 정한 조건 | IRQ 핸들러·워크큐 |

그래서 G6에서 드라이버 `.poll`을 직접 짤 때, "eventfd처럼 카운터와 wait queue를 두고 `.poll`에서 `poll_wait` + 조건 검사, 데이터가 생기면 `wake_up`"이라는 똑같은 패턴을 쓰게 된다. wait queue 자체(`wait_event_interruptible`, `wake_up`, 엔트리 구조)는 F3에서 자세히 다룬다.

### 10.6 드라이버를 쓸 때 다시 만나는 지점

- **G4/G6**: 문자 디바이스에 `.poll` 구현. `poll_wait` + mask 리턴 + IRQ 경로에서 `wake_up_interruptible`. mask를 잘못 계산하면 유저 epoll 루프가 busy loop(항상 ready)이 되거나 영원히 안 깨어난다.
- **F3**: wait queue와 `wait_event_interruptible` — blocking `.read`와 `.poll`이 **같은 wait queue를 공유**하는 이유.
- **F2**: `ep_poll_callback`은 wake_up을 부른 문맥에서 돈다. 드라이버가 hardirq에서 `wake_up`을 불러도 되는 이유(콜백이 잠들지 않는 일만 하므로)와, 그래도 무거운 일은 threaded IRQ로 미루는 이유.
- **G8**: sysfs 속성에 변경 알림을 주려면 드라이버에서 `sysfs_notify()`를 부른다 → 유저는 §3.3처럼 POLLPRI로 받는다.
- **I2(IIO)**: IIO buffered capture의 `/dev/iio:deviceN`은 바로 이 구조(kfifo + wait queue + `.poll`)로 되어 있다. IMU 데몬은 이 fd를 epoll에 넣는다.

---

## 11. Embedded 실무에서 다시 보기

### 11.1 센서 키트 메인 데몬

예를 들어 센서 키트라면 메인 데몬 하나가 이런 fd들을 epoll에 넣는다.

- `/dev/iio:device0` (IMU, IIO buffered, `POLLIN`) — 1 kHz 샘플이 FIFO 워터마크마다 묶여서 옴
- `/dev/video0` (V4L2 카메라, `POLLIN` = 채워진 버퍼가 dequeue 가능, I5) — `VIDIOC_DQBUF`
- UDP 소켓 (LiDAR, `POLLIN`) — ET라면 `recvmmsg`로 `EAGAIN`까지 drain
- CAN 소켓 (`AF_CAN`, SocketCAN, I10) — 모터 컨트롤러 상태 프레임
- timerfd (상태 퍼블리시 10 Hz, watchdog 킥)
- signalfd (SIGTERM, SIGHUP 설정 리로드)
- eventfd (영상 인코딩 worker 완료 알림)

전부 fd라서 **이벤트 루프 코드는 장치 종류와 무관하게 똑같다**. 이것이 "everything is a file"(A1)의 실무적 가치다.

### 11.2 GPIO 인터럽트 핀 (data-ready, 비상 정지 버튼)

센서의 DRDY 핀이나 E-stop 버튼을 유저스페이스에서 받을 때, 레거시 BSP는 §3.3의 sysfs GPIO + `POLLPRI`를 쓰고, 최신 코드는 libgpiod(`gpiod_line_request_get_fd()`로 얻은 fd를 epoll에 넣고 `POLLIN` → 이벤트 read)를 쓴다. 어느 쪽이든 유저스페이스 경로는 **IRQ → 커널 → wake_up → 스케줄링 → 유저 깨어남**이라 수십 us~수 ms의 지연과 지터가 있다. 하드 실시간 응답(모터 즉시 정지)은 하드웨어나 MCU(N4)가 맡고 Linux는 기록·상위 판단만 하는 설계가 일반적이다.

### 11.3 systemd watchdog을 timerfd로

systemd 서비스에 `WatchdogSec=2s`를 걸면, 데몬은 그 절반 주기 정도로 `sd_notify("WATCHDOG=1")`을 보내야 한다(L1). timerfd 1초 주기를 이벤트 루프에 넣고 그 핸들러에서 킥한다. 이렇게 하면 **이벤트 루프가 어딘가에서 막혔을 때만** 킥이 멈춰서 systemd가 재시작해 준다 — 별도 스레드에서 킥하면 메인 루프가 죽어도 킥이 계속되어 watchdog이 무의미해진다. 펌웨어에서 "watchdog은 main loop에서만 킥한다"는 원칙과 같다.

### 11.4 overrun 로그로 지연 문제 잡기

§6의 `expirations > 1`을 카운터로 남겨 두면, 현장에서 "가끔 IMU 데이터가 튄다"는 리포트가 왔을 때 "그 시각에 루프가 300 ms 막혔다"를 바로 증명할 수 있다. 그 다음 strace `-T`(syscall 소요 시간)나 `perf trace`로 어느 핸들러가 막았는지 찾는다(M 모듈). Don이 SSD 펌웨어에서 latency outlier를 타임스탬프 로그로 추적하던 방식과 같다.

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| ET인데 한 번만 read | 가끔 데이터가 늦게 처리되거나 멈춤 | 다음 edge까지 재알림 없음 | non-blocking + `EAGAIN`까지 drain, 또는 LT 사용 |
| `POLLHUP`/EOF를 처리 안 함 | CPU 100%, 루프가 쉬지 않음 | HUP은 항상 보고되어 계속 즉시 리턴 | `read()==0`이면 fd를 epoll에서 빼고 close |
| sysfs 파일을 `POLLIN`으로 기다림 | 이벤트 없이 계속 깨어남 | sysfs는 항상 읽기 가능 | `POLLPRI` + `POLLERR`, 깨면 offset 0부터 다시 read |
| select에 fd ≥ 1024 | 메모리 손상 또는 fortify abort | `fd_set`이 1024비트 고정 | poll 또는 epoll로 교체 |
| select 후 `fd_set`/timeout 재설정 안 함 | 두 번째부터 이벤트 놓침, 즉시 timeout | in-out 인자라 덮어써짐 | 루프마다 `FD_ZERO`/`FD_SET`, `tv` 재설정 |
| `EPOLLONESHOT` 후 re-arm 누락 | 그 fd가 영원히 조용함 | 알림 후 disarm됨 | 처리 끝에 `EPOLL_CTL_MOD` |
| close 전에 DEL 안 하고 dup된 fd 남김 | 닫은 fd 이벤트가 계속 옴 | epoll은 열린 파일 설명(open file description)이 모두 닫혀야 자동 제거 | close 전에 `EPOLL_CTL_DEL` |
| 핸들러에서 blocking 호출(sleep, 느린 파일 쓰기) | 다른 이벤트 전부 지연, timerfd overrun | 스레드 하나라 루프가 멈춤 | worker 스레드 + eventfd로 결과 받기 |
| signalfd 쓰면서 시그널 block 안 함 | 시그널이 와서 프로세스가 그냥 죽음 | 기본 동작이 먼저 실행 | 모든 스레드에서 `sigprocmask`/`pthread_sigmask`로 block |
| strace 필터에 `epoll_wait`, `select`, `poll`만 넣음 (arm64) | 아무 것도 안 잡힘 | arm64는 `epoll_pwait`, `pselect6`, `ppoll`만 있음 | 필터에 p 버전 추가, 또는 `-e trace=%desc` |

---

## 13. 면접에서 이렇게 말한다

**Q.** What is the difference between select, poll and epoll?

**A.** 세 가지 모두 여러 fd 중 준비된 것을 기다리는 방법이다. select는 1024비트 고정 비트맵이라 fd 번호 제한이 있고 매번 다시 채워야 한다. poll은 배열이라 제한이 없지만 매 호출마다 N개를 커널에 복사하고 N개 모두 `.poll`을 부른다. epoll은 관심 목록을 커널에 한 번 등록하고, 이벤트가 생긴 fd만 콜백이 ready list에 넣기 때문에 대기 비용이 준비된 fd 수에만 비례한다. 직접 재 보니 fd 5000개 중 하나만 활성일 때 poll은 한 번에 약 220 us, epoll은 약 0.6 us였다.

> select uses a fixed 1024-bit bitmap that you rebuild on every call; poll takes an array, so no fd limit, but it still copies all N entries and calls every file's poll method on each call, so it's O(N). epoll registers the interest set once in the kernel and installs a callback on each file's wait queue; when a file becomes ready, the callback puts it on a ready list, so epoll_wait costs O(ready), not O(N). In my own measurement with 5,000 pipes and one active, poll took about 220 microseconds per wake-up and epoll about 0.6, flat.

**꼬리 질문**: "그럼 fd가 5개면?" → poll로 충분하다, 차이가 1 us 미만이고 코드가 단순하다.

**Q.** Explain level-triggered versus edge-triggered epoll. What's the classic bug?

**A.** LT는 데이터가 남아 있는 동안 매번 알려 주고, ET는 상태가 바뀔 때(새 데이터 도착) 한 번만 알려 준다. 펌웨어의 level/edge 인터럽트와 같다. 고전적인 버그는 ET에서 버퍼를 다 비우지 않는 것이다. 남은 데이터는 다음 패킷이 올 때까지 알림이 없어서 지연이나 멈춤이 생긴다. 그래서 ET는 반드시 non-blocking fd로 `EAGAIN`까지 읽는다.

> Level-triggered reports the fd as long as the condition holds, like a level-sensitive interrupt. Edge-triggered reports only on a transition, like an edge-sensitive interrupt. The classic bug is using edge-triggered mode and only reading part of the data: there's no new edge, so the remaining bytes sit there until the next packet arrives. The rule is: with EPOLLET, make the fd non-blocking and drain until EAGAIN. For a sensor daemon I default to level-triggered.

**Q.** How would you structure a Linux daemon that handles an IMU, a LiDAR UDP stream, a periodic timer, and clean shutdown?

**A.** 단일 이벤트 루프로 설계한다. IMU는 IIO 버퍼 fd, LiDAR는 UDP 소켓, 주기는 timerfd, 종료는 SIGTERM을 block하고 signalfd로 받는다. 전부 epoll에 넣고 `data.ptr`로 핸들러를 찾는다. 핸들러는 짧게 유지하고, 무거운 처리는 worker 스레드로 보낸 뒤 eventfd로 완료를 받는다. timerfd의 만료 카운트로 overrun을 감지해 로그로 남기고, systemd watchdog 킥도 이 루프 안에서 한다.

> One epoll-based event loop. The IMU is an IIO buffer fd, the LiDAR is a non-blocking UDP socket, the periodic work is a timerfd on CLOCK_MONOTONIC, and SIGTERM is blocked and read through a signalfd so shutdown is just another event. Each registration carries a pointer to its handler in data.ptr. Handlers never block; heavy work goes to a worker thread that signals completion through an eventfd. I log timerfd overruns, the expiration count above one, and kick the systemd watchdog from inside the loop so a stalled loop actually triggers a restart.

**Q.** How does a character device driver support poll/select/epoll?

**A.** `file_operations`에 `.poll`을 구현한다. 그 안에서 `poll_wait()`로 장치의 wait queue를 등록하고, 지금 상태를 mask(`EPOLLIN | EPOLLRDNORM` 등)로 리턴한다. `poll_wait`는 잠들지 않는다. 데이터가 생기면 IRQ 경로에서 같은 wait queue에 `wake_up_interruptible()`을 부른다. 그러면 select/poll은 다시 `.poll`을 불러 확인하고, epoll은 `ep_poll_callback`이 해당 항목을 ready list에 넣는다. blocking `read`도 같은 wait queue를 쓴다.

> You implement the poll method in file_operations. It calls poll_wait() with the device's wait queue, which doesn't sleep, it just registers the caller, and then returns the current readiness mask, for example EPOLLIN | EPOLLRDNORM when the FIFO isn't empty. When new data arrives, the interrupt path calls wake_up_interruptible() on that same wait queue. For select and poll that wakes the sleeping task, which re-polls; for epoll the wait-queue entry's callback, ep_poll_callback, moves the item onto the ready list. The blocking read path sleeps on the same queue.

**꼬리 질문**: "poll이 항상 EPOLLIN을 리턴하면?" → 유저 epoll 루프가 busy loop가 된다. mask는 반드시 실제 상태를 반영해야 한다.

**Q.** Why use eventfd instead of a pipe to wake up an event loop from another thread?

**A.** eventfd는 커널의 64비트 카운터 하나라서 fd 하나, 버퍼 할당 없음, 여러 번 써도 합쳐져서 넘치지 않는다. pipe는 fd 두 개에 버퍼가 차면 writer가 막힐 수 있다. eventfd는 알림 횟수만 전달하므로 실제 데이터는 락으로 보호한 큐에 넣고, eventfd는 "큐 확인해"라는 초인종으로 쓴다.

> An eventfd is a single 64-bit counter in the kernel: one fd instead of two, no buffer, and multiple writes coalesce, so it can't fill up and block the writer the way a pipe can. It only carries a count, so I put the actual work items in a locked queue and use the eventfd purely as the doorbell, the same mailbox-plus-doorbell pattern I used between cores in firmware.

**Q.** How do you run a periodic 100 Hz task in user space, and how do you detect if you missed deadlines?

**A.** `usleep` 루프는 처리 시간만큼 주기가 밀린다. timerfd를 `CLOCK_MONOTONIC`, interval 10 ms로 만들고 epoll에 넣는다. read하면 마지막 read 이후 만료 횟수가 오는데, 1보다 크면 그만큼 tick을 놓친 것이다. 이걸 카운터로 남기면 현장에서 지연 문제를 증명할 수 있다. 지터 자체가 문제면 SCHED_FIFO, CPU 격리, PREEMPT_RT를 검토한다.

> A sleep loop drifts by the processing time. I use a timerfd on CLOCK_MONOTONIC with a 10 ms interval inside the event loop. Reading it returns the number of expirations since the last read, so anything above one is a missed period, and I count and log it. If the jitter itself is the problem, the next steps are SCHED_FIFO for the thread, CPU isolation, and a PREEMPT_RT kernel, measured with cyclictest.

**Q.** Can you use epoll on a regular file?

**A.** 안 된다. 일반 파일은 `.poll`이 없어서 `epoll_ctl`이 `EPERM`을 리턴하고, poll은 항상 준비됨으로 답한다. 디스크 I/O는 "준비" 개념이 없기 때문이다. 이벤트 루프에서 파일 I/O가 필요하면 worker 스레드에 맡기거나 io_uring을 쓴다.

> No. Regular files don't implement a poll method, so epoll_ctl returns EPERM, and poll just reports them as always readable and writable. Disk I/O has no readiness concept. If the loop needs file I/O, I hand it to a worker thread, or use io_uring, which is a completion model rather than a readiness model, similar to NVMe submission and completion queues.

**Q.** What is EPOLLONESHOT for, and what is EPOLLEXCLUSIVE?

**A.** ONESHOT은 여러 스레드가 한 epoll을 공유할 때 같은 fd가 동시에 두 스레드에 배달되는 것을 막는다. 한 번 알리면 꺼지고, 처리 스레드가 `EPOLL_CTL_MOD`로 다시 켠다. EXCLUSIVE는 여러 epoll 인스턴스가 같은 fd를 기다릴 때 전부 깨어나는 thundering herd를 줄인다(4.5+).

> EPOLLONESHOT disables the fd after one event, so when several threads share one epoll set, the same socket isn't handed to two threads at once; the worker re-arms it with EPOLL_CTL_MOD when it's done. EPOLLEXCLUSIVE, since 4.5, is for the opposite layout, many epoll instances watching the same fd like a listening socket, and it avoids waking all of them for one event.

---

## 14. 직접 해보기

1. (개념) select가 `nfds` 인자로 "최대 fd + 1"을 받는 이유는 무엇이고, fd 3 하나만 감시하더라도 fd 1000을 열어 둔 프로세스라면 무엇이 비효율적인가?
   정답/힌트: 커널은 0..nfds-1 비트를 다 훑는다. 감시 fd가 큰 번호면 앞쪽 빈 비트까지 스캔한다(실제 감시 fd 수와 무관하게 maxfd에 비례).

2. (코드) ex5를 고쳐 ET 모드에서 "4바이트씩 읽되 drain하지 않는" 대신, 0.05초 뒤에 1바이트를 더 write하면 어떻게 되는지 확인하라. 깨어났을 때 남은 바이트 수는?
   정답/힌트: 새 write가 새 edge를 만들어 다시 깨어난다. 이때 남은 데이터는 6+1=7바이트 — "늦게라도 처리되지만 지연된다"는 ET 버그의 실제 모습이다.

3. (코드) ex9에 §7의 signalfd를 추가해서 `kill -TERM <pid>`로 루프를 깔끔하게 끝내도록 만들어라. stdin `quit`과 같은 종료 경로를 타게 하라.
   정답/힌트: `pthread_create` 전에 `sigprocmask(SIG_BLOCK)`(스레드가 마스크를 상속), `watch(sfd, SRC_SIGNAL)`, 핸들러에서 `struct signalfd_siginfo`를 읽고 `running = 0`.

4. (관찰) ex9 실행 중에 `cat /proc/<pid>/fdinfo/<timerfd>`를 여러 번 찍어 `it_value`가 줄어드는 것과 `ticks`를 관찰하라. 루프가 timerfd를 읽지 않게(그 case를 주석 처리) 바꾸면 `ticks`와 CPU 사용률은 어떻게 되나?
   정답/힌트: `ticks`가 계속 증가하고, LT라 epoll_wait가 즉시 리턴해서 CPU 100% busy loop가 된다(`top`으로 확인).

5. (관찰) `strace -c ./ex11 2000 1`과 `strace -c ./ex11 2000 2`(ulimit -n 올린 뒤)를 비교하라. syscall 횟수는 같은데 왜 시간은 수십 배 차이 나나?
   정답/힌트: syscall 횟수는 같고(`ppoll` vs `epoll_pwait` 각 1000번) 한 번의 커널 내부 일의 양이 다르다(N개 `.poll` vs ready 1개). §10.4의 bpftrace 결과가 그 내부다.

6. (설계) 카메라 30 fps(V4L2), IMU 1 kHz(IIO, 워터마크 32샘플), LiDAR 10 Hz UDP를 한 데몬에서 받는다. 각 fd를 LT/ET 중 무엇으로, 각 핸들러에서 한 번에 얼마나 읽을지 정하고 이유를 써라.
   정답/힌트: 기본 LT. IMU는 워터마크 단위로 깨어나므로 FIFO에 있는 만큼 읽기, UDP는 `recvmmsg`로 여러 패킷 한 번에. 카메라 DQBUF 후 처리는 worker로 넘기고 버퍼 반환(QBUF)은 eventfd로 돌아와서.

7. (관찰) ex3을 sysfs GPIO 대신 다른 sysfs 속성에도 적용해 보라. 예: gpio-sim의 `sim_gpioN/value`(출력 라인) 또는 `/proc/self/mounts`(마운트 변화 시 `POLLPRI`/`POLLERR`, proc(5) 참고).
   정답/힌트: `/proc/self/mounts`는 `mount`/`umount`가 일어날 때 poll이 `POLLERR|POLLPRI`로 깨어난다. sudo로 tmpfs를 마운트해 보며 확인한다.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| I/O multiplexing | I/O 다중화 | 한 스레드가 여러 fd를 동시에 기다리는 기법 |
| readiness | 준비 상태 | 지금 read/write해도 막히지 않는 상태. epoll이 알려 주는 것 |
| `fd_set` | select용 비트맵 | 1024비트 고정, fd 번호 = 비트 번호 |
| `FD_SETSIZE` | fd_set 크기 | 1024. 이 이상 번호의 fd는 select로 못 씀 |
| `struct pollfd` | poll용 엔트리 | fd, events(입력), revents(출력) |
| `POLLPRI` | 예외적 조건 | TCP urgent, sysfs 변경 알림, cgroup.events 등 |
| `POLLHUP` | 끊김 | 상대가 닫음. 요청 안 해도 항상 보고 |
| `POLLNVAL` | 잘못된 fd | 열리지 않은 fd를 넘김 |
| epoll instance | epoll 객체 | `struct eventpoll`. 관심 목록 + ready list + 대기자 |
| interest list | 관심 목록 | `epoll_ctl`로 등록한 fd들. 커널 rb-tree(`rbr`) |
| ready list | 준비 목록 | 이벤트가 생긴 epitem 연결 리스트(`rdllist`) |
| `epitem` | epoll 항목 | 등록된 fd 하나를 나타내는 커널 구조체 |
| level-triggered (LT) | 레벨 트리거 | 조건이 유지되는 동안 매번 보고(기본값) |
| edge-triggered (ET) | 엣지 트리거 | 상태 변화 때 한 번만 보고(`EPOLLET`) |
| `EPOLLONESHOT` | 일회성 | 한 번 보고 후 disarm, `EPOLL_CTL_MOD`로 재무장 |
| `EPOLLEXCLUSIVE` | 배타적 깨우기 | 같은 fd를 보는 여러 epoll 중 일부만 깨움(4.5+) |
| thundering herd | 우르르 깨어남 | 이벤트 하나에 대기자 전부가 깨는 낭비 |
| eventfd | 이벤트 카운터 fd | 64비트 카운터. write는 더하기, read는 꺼내고 0 |
| timerfd | 타이머 fd | hrtimer 만료를 fd로. read하면 만료 횟수 |
| overrun | 놓친 주기 | timerfd 만료 횟수가 1보다 큰 것 |
| signalfd | 시그널 fd | block한 시그널을 read로 받는 fd |
| `anon_inode` | 익명 inode | 디스크 파일 없이 커널 객체에 fd를 붙이는 방식 |
| `.poll` | 드라이버 poll 메서드 | `poll_wait` 등록 + 현재 mask 리턴 |
| `poll_wait()` | 대기 등록 함수 | wait queue를 poll_table에 알림. 잠들지 않음 |
| wait queue | 대기 큐 | 이벤트를 기다리는 엔트리 목록. `wake_up`으로 깨움 |
| `ep_poll_callback` | epoll 콜백 | wake_up 시 epitem을 ready list에 넣는 함수 |
| io_uring | 비동기 I/O 링 | SQ/CQ 링버퍼 기반 completion 모델(5.1+) |

---

## 16. 요약 & 체크리스트

스레드 하나가 여러 이벤트 소스를 기다려야 할 때, Linux는 모든 소스를 fd로 만들고(소켓·장치는 원래 fd, 타이머는 timerfd, 스레드 알림은 eventfd, 시그널은 signalfd) select/poll/epoll로 한 번에 기다린다. select는 1024 제한과 재무장 비용, poll은 매 호출 O(N) 비용이 있고, epoll은 관심 목록을 커널에 한 번 등록한 뒤 드라이버 wait queue에 건 콜백(`ep_poll_callback`)이 ready list를 채우므로 대기 비용이 준비된 fd 수에만 비례한다 — 직접 측정에서 N=5000일 때 poll 222 us, epoll 0.56 us였다. LT는 안전한 기본값이고, ET는 non-blocking + `EAGAIN`까지 drain을 지켜야 한다. 이 모든 것은 드라이버의 `.poll`(= `poll_wait` + mask)과 IRQ 경로의 `wake_up` 위에 서 있고, 이것이 G6에서 직접 구현할 부분이다.

- [ ] blocking + 스레드 방식과 이벤트 루프 방식의 장단점을 펌웨어 비유로 설명할 수 있다
- [ ] select의 `fd_set`, `FD_SETSIZE`, 재무장 문제를 설명할 수 있다
- [ ] poll의 `revents` 비트(IN/OUT/PRI/ERR/HUP/NVAL)가 언제 켜지는지 말할 수 있다
- [ ] sysfs 속성 파일은 `POLLPRI|POLLERR`로 기다리고 깨면 offset 0부터 다시 읽어야 한다는 것을 안다
- [ ] `epoll_create1` / `epoll_ctl` / `epoll_wait`로 이벤트 루프를 짤 수 있다
- [ ] LT와 ET의 차이와 ET의 drain 규칙을 코드로 보여 줄 수 있다
- [ ] `EPOLLONESHOT` 재무장, `EPOLLEXCLUSIVE`의 용도를 말할 수 있다
- [ ] eventfd의 카운터 의미(합쳐짐, semaphore 모드)를 설명하고 worker → 루프 알림에 쓸 수 있다
- [ ] timerfd로 drift 없는 주기 작업을 만들고 overrun을 감지할 수 있다
- [ ] 드라이버 `.poll`, `poll_wait`, `wake_up`, `ep_poll_callback`으로 이어지는 경로를 그릴 수 있다
- [ ] select/poll/epoll의 확장성 차이를 측정 숫자로 말할 수 있다
- [ ] arm64에서 strace에 `pselect6`/`ppoll`/`epoll_pwait`로 보인다는 것을 안다

---

## 참고 자료

- man7.org: [select(2)](https://man7.org/linux/man-pages/man2/select.2.html), [poll(2)](https://man7.org/linux/man-pages/man2/poll.2.html), [epoll(7)](https://man7.org/linux/man-pages/man7/epoll.7.html), [epoll_ctl(2)](https://man7.org/linux/man-pages/man2/epoll_ctl.2.html), [epoll_wait(2)](https://man7.org/linux/man-pages/man2/epoll_wait.2.html), [eventfd(2)](https://man7.org/linux/man-pages/man2/eventfd.2.html), [timerfd_create(2)](https://man7.org/linux/man-pages/man2/timerfd_create.2.html), [signalfd(2)](https://man7.org/linux/man-pages/man2/signalfd.2.html)
- Michael Kerrisk, "The Linux Programming Interface", 63장 Alternative I/O Models (select, poll, signal-driven I/O, epoll)
- docs.kernel.org: [GPIO Sysfs Interface for Userspace (obsolete)](https://docs.kernel.org/admin-guide/gpio/sysfs.html), [gpio-sim](https://docs.kernel.org/admin-guide/gpio/gpio-sim.html)
- 커널 소스(elixir.bootlin.com): `fs/eventpoll.c`(`ep_insert`, `ep_poll_callback`, `ep_send_events`), `fs/select.c`(`do_sys_poll`, `__pollwait`, `pollwake`), `include/linux/poll.h`(`poll_wait`), `fs/eventfd.c`, `fs/timerfd.c`, `fs/pipe.c`(`pipe_poll`), `fs/kernfs/file.c`(`kernfs_fop_poll`, `kernfs_notify`)
- Linux Device Drivers 3판(LDD3) 6장 "Advanced Char Driver Operations" — poll과 select 절 (API 일부는 옛날 것이므로 현재 커널 소스와 대조)
- Bootlin, "Linux kernel and driver development" 교육 슬라이드 — 문자 드라이버와 sleeping/wait queue 부분
- OSTEP 33장 "Event-based Concurrency (Advanced)"
