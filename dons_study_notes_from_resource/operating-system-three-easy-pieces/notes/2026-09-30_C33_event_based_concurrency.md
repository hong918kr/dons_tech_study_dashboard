# Ch.33 이벤트 기반 동시성 — 스레드 없이 동시에 여러 일을 처리하기

> 📖 원문: [33. Event-based Concurrency (Advanced)](../book-md/C33_event_based_concurrency_advanced.md) · [PDF p.394](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=394) · ⏱️ 읽기 약 35분 · 🔗 선행: [Ch.26](2026-09-30_C26_concurrency_intro.md), [Ch.32](2026-09-30_C32_concurrency_bugs.md)

## 0. 한눈에 보기

- 스레드 기반 동시성의 두 가지 고통: (1) 락 누락·교착 같은 **버그**, (2) 무엇이 언제 돌지 **OS 스케줄러에 맡겨야** 하는 것. 이벤트 기반 동시성은 이 둘을 피하려는 다른 스타일이다 (GUI, node.js, nginx 류 서버).
- 구조는 단순하다: **이벤트 루프** — `while(1) { events = getEvents(); for (e in events) processEvent(e); }`. 한 번에 핸들러 하나만 도니 **락이 필요 없고**, "다음에 어떤 이벤트를 처리할지"가 곧 **스케줄링**이다.
- 이벤트를 받는 API: `select()` / `poll()` (매번 fd 집합을 넘김), 확장판 `epoll`(리눅스) / **`kqueue`(macOS·BSD)** (한 번 등록, 준비된 것만 돌려받음).
- 대가: **절대 블로킹하면 안 된다**. 디스크 I/O 는 **비동기 I/O(AIO)** 로, 그러면 I/O 가 끝났을 때 이어서 할 일을 기억하는 **상태 관리(continuation, 수동 스택 관리)** 가 필요해진다. 멀티코어, 페이지 폴트(암묵적 블로킹), API 의미 변화에는 여전히 약하다.

> **THE CRUX: HOW TO BUILD CONCURRENT SERVERS WITHOUT THREADS** — "How can we build a concurrent server without using threads, and thus retain control over concurrency as well as avoid some of the problems that seem to plague multi-threaded applications?"
>
> → 스레드를 쓰지 않고 어떻게 동시성 서버를 만들어서, 동시성에 대한 제어권을 유지하면서 멀티스레드 프로그램을 괴롭히는 문제들을 피할 수 있을까?

## 1. 5분 복습표

| 용어 | 한 줄 뜻 | 예시 / 비유 |
|---|---|---|
| 이벤트 기반 동시성(event-based concurrency) | 이벤트를 기다렸다가 하나씩 처리하는 단일 루프 스타일 | node.js, nginx, GUI |
| 이벤트 루프(event loop) | `getEvents()` → 각 이벤트 처리를 무한 반복 | `while(1)` 메인 루프 |
| 이벤트 핸들러(event handler) | 이벤트 하나를 처리하는 짧은 코드 | `processFD(fd)` |
| select() | fd 비트 집합을 넘겨 준비된 것을 표시받음 | `FD_SET`, `FD_ISSET` |
| poll() | select 와 같은 역할, `struct pollfd` 배열 사용 | fd 개수 제한 완화 |
| epoll / kqueue | 관심 fd 를 커널에 **한 번 등록**, 준비된 것만 돌려받음 | 리눅스 / macOS·BSD |
| 블로킹(blocking, 동기) 인터페이스 | 일을 다 끝내고 리턴 | `read()` 가 디스크 기다림 |
| 논블로킹(non-blocking, 비동기) 인터페이스 | 일을 시작만 하고 즉시 리턴 | `O_NONBLOCK`, `aio_read()` |
| 비동기 I/O(AIO) | I/O 요청 후 즉시 리턴, 완료는 나중에 확인 | `aio_read` + `aio_error` |
| aiocb | AIO 제어 블록: fd, offset, buf, nbytes | `struct aiocb` |
| 시그널(signal) | 프로세스에 보내는 비동기 알림, 핸들러 실행 | `SIGHUP`, `SIGSEGV` |
| continuation | "이 I/O 가 끝나면 이어서 할 일"에 필요한 상태를 저장해 둔 것 | fd → sd 해시 테이블 |
| 수동 스택 관리(manual stack management) | 스레드라면 스택에 있었을 상태를 직접 저장/복원 | 콜백 + 컨텍스트 구조체 |

## 2. 기본 아이디어: 이벤트 루프 (33.1)

```c
while (1) {
    events = getEvents();
    for (e in events)
        processEvent(e);
}
```

- 이벤트(event) = 무언가 일어남 (패킷 도착, 타이머 만료, I/O 완료).
- 핸들러가 이벤트 하나를 처리하는 동안 **시스템에서 일어나는 일은 그것뿐**이다. 그래서 **어떤 이벤트를 다음에 처리할지 고르는 것 = 스케줄링**. 애플리케이션이 스케줄링을 직접 쥔다 — 이벤트 방식의 근본적 장점.

남은 질문: 네트워크/디스크에서 "무슨 이벤트가 일어났는지" 를 어떻게 알아내나?

## 3. 중요한 API: select() / poll() (33.2)

```c
int select(int nfds,
           fd_set *restrict readfds,
           fd_set *restrict writefds,
           fd_set *restrict errorfds,
           struct timeval *restrict timeout);
```

macOS man page 요약: 세 집합에 든 fd 들이 **읽을 준비 / 쓸 준비 / 예외 상황** 인지 검사한다. 0 부터 `nfds-1` 까지 본다. 리턴할 때 각 집합을 **준비된 fd 만 남긴 부분집합으로 덮어쓰고**, 준비된 fd 총수를 돌려준다.

- **읽기 집합**: 새 패킷이 도착해 처리할 게 있는지. **쓰기 집합**: 응답을 보내도 되는지(송신 큐가 안 찼는지).
- **timeout**: `NULL` 이면 뭔가 준비될 때까지 무한정 블록. `0` 이면 즉시 리턴(폴링). 견고한 서버는 보통 타임아웃을 준다 (주기적 작업, 타이머 처리용).
- `poll()` 도 거의 같다 (`struct pollfd` 배열에 fd 와 관심 이벤트를 적음).

> **ASIDE — BLOCKING VS. NON-BLOCKING INTERFACES**: 블로킹(동기) 인터페이스는 일을 다 끝내고 리턴하고, 논블로킹(비동기) 인터페이스는 일을 시작만 하고 즉시 리턴해서 나머지는 백그라운드에서 진행된다. 블로킹의 흔한 원인은 I/O. 논블로킹 인터페이스는 어떤 스타일(스레드 포함)에서도 쓸 수 있지만 **이벤트 방식에서는 필수**다 — 블로킹 호출 하나가 모든 진행을 멈추니까.

**select 의 비용 (새 예제로 계산)**: 서버가 연결 10,000 개를 갖고 있고 한 번에 10 개만 활동한다고 하자.

```text
select():  매 호출마다 사용자 → 커널로 fd_set 복사 (10,000 비트 = 1,250 B x 최대 3 집합)
           커널이 10,000 개 fd 를 전부 검사 + 사용자도 FD_ISSET 로 10,000 개를 다시 훑음
           → 호출당 O(전체 fd 수) = ~20,000 번 검사로 10 개를 찾음 (효율 0.05%)
kqueue/epoll: 관심 fd 는 한 번만 등록, kevent()/epoll_wait() 는 준비된 10 개만 배열로 돌려줌
           → 호출당 O(준비된 fd 수) = 10
```

게다가 macOS 의 `fd_set` 은 기본 `FD_SETSIZE` = 1024 비트라 그 이상의 fd 번호는 select 로 다룰 수조차 없다. 그래서 C10K(동시 연결 1만 개) 시대에 리눅스는 `epoll`, BSD/macOS 는 `kqueue` 를 만들었다.

## 4. select() 사용하기 (33.3)

```c
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

int main(void) {
    // open and set up a bunch of sockets (not shown)
    // main loop
    while (1) {
        // initialize the fd_set to all zero
        fd_set readFDs;
        FD_ZERO(&readFDs);
        // now set the bits for the descriptors this server is interested in
        // (for simplicity, all of them from min to max)
        int fd;
        for (fd = minFD; fd < maxFD; fd++)
            FD_SET(fd, &readFDs);
        // do the select
        int rc = select(maxFD+1, &readFDs, NULL, NULL, NULL);
        // check which actually have data using FD_ISSET()
        for (fd = minFD; fd < maxFD; fd++)
            if (FD_ISSET(fd, &readFDs))
                processFD(fd);
    }
}
```

(Fig 33.1. 원문은 루프 안에서 `int fd;` 를 두 번 선언하는 오타가 있어 하나로 합쳤다.) 매 반복마다 `FD_ZERO` → 관심 fd 를 `FD_SET` → `select()` → `FD_ISSET` 으로 준비된 것만 처리. 집합을 **매번 다시 만들어야** 하는 이유: select 가 집합을 결과로 덮어쓰기 때문.

```svg
<svg viewBox="0 0 700 300" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs>
    <marker id="C33-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="350" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">이벤트 루프: 커널이 "준비됨"을 알려 주고, 단일 스레드가 하나씩 처리</text>
  <rect x="20" y="45" width="660" height="70" fill="none" stroke="currentColor" stroke-dasharray="5,4"/>
  <text x="30" y="62" fill="currentColor">커널</text>
  <rect x="60" y="72" width="80" height="30" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="100" y="92" text-anchor="middle" fill="currentColor">sock A</text>
  <rect x="150" y="72" width="80" height="30" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="190" y="92" text-anchor="middle" fill="currentColor">sock B</text>
  <rect x="240" y="72" width="80" height="30" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="280" y="92" text-anchor="middle" fill="currentColor">timer</text>
  <rect x="330" y="72" width="90" height="30" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="375" y="92" text-anchor="middle" fill="currentColor">AIO 완료</text>
  <rect x="470" y="65" width="190" height="42" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
  <text x="565" y="83" text-anchor="middle" fill="currentColor">ready list</text>
  <text x="565" y="100" text-anchor="middle" fill="currentColor">(kqueue/epoll 이 유지)</text>
  <line x1="422" y1="87" x2="468" y2="87" stroke="currentColor" marker-end="url(#C33-arrow)"/>
  <rect x="200" y="160" width="300" height="44" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="350" y="180" text-anchor="middle" fill="currentColor">events = kevent() / select()</text>
  <text x="350" y="197" text-anchor="middle" fill="currentColor">(준비된 게 없으면 여기서만 잠든다)</text>
  <line x1="565" y1="108" x2="470" y2="158" stroke="currentColor" marker-end="url(#C33-arrow)"/>
  <rect x="200" y="240" width="300" height="40" fill="none" stroke="currentColor"/>
  <text x="350" y="265" text-anchor="middle" fill="currentColor">for e in events: handler(e)  ← 짧게, 절대 블록 금지</text>
  <line x1="350" y1="206" x2="350" y2="238" stroke="currentColor" marker-end="url(#C33-arrow)"/>
  <path d="M500,260 C600,260 600,182 502,182" fill="none" stroke="currentColor" marker-end="url(#C33-arrow)"/>
  <text x="600" y="225" fill="currentColor">반복</text>
  <text x="20" y="150" fill="currentColor">핸들러가</text>
  <text x="20" y="168" fill="currentColor">블록하면</text>
  <text x="20" y="186" fill="#d9534f">루프 전체 정지</text>
</svg>
```

## 5. 왜 더 단순한가: 락이 필요 없다 (33.4)

CPU 하나 + 이벤트 방식이면, 한 번에 이벤트 하나만 처리되므로 **락을 잡고 놓을 필요가 없다**. 다른 스레드가 끼어들 수 없다 — 애초에 스레드가 하나다. Ch.32 의 원자성 위반, 교착 같은 버그가 기본 이벤트 방식에서는 나타나지 않는다.

> **TIP — DON'T BLOCK IN EVENT-BASED SERVERS**: 이벤트 서버는 작업 스케줄링을 세밀하게 제어하게 해 준다. 그 제어를 유지하려면 호출자를 블록시키는 호출을 **절대** 하면 안 된다. 어기면 서버가 멈추고, 클라이언트는 화나고, 당신이 이 장을 읽긴 했는지 의심받는다.

## 6. 문제: 블로킹 시스템 콜 (33.5)

클라이언트가 디스크의 파일을 요청한다 (단순 HTTP 요청처럼). 핸들러는 `open()` 후 `read()` 를 여러 번 해야 하는데, 메타데이터나 데이터가 메모리에 없으면 **둘 다 디스크 I/O 를 기다리며 블록**될 수 있다.

- 스레드 서버: 그 스레드만 잠들고 다른 스레드가 돈다. I/O 와 계산이 자연스럽게 겹친다 — 스레드 프로그래밍이 쉬운 이유.
- 이벤트 서버: 다른 스레드가 없다. 핸들러가 블록하면 **서버 전체**가 블록한다. 그동안 시스템은 논다.

그래서 규칙: **이벤트 기반 시스템에서 블로킹 호출은 허용되지 않는다.** (11.1 에서 핸들러 하나에 200ms 블로킹을 넣었더니 다른 클라이언트의 대기 지연이 0.1ms → 620ms 로 뛰었다.)

## 7. 해법: 비동기 I/O (33.6)

현대 OS 는 디스크 I/O 를 **요청만 하고 즉시 리턴**하는 인터페이스를 준다. macOS(POSIX AIO):

```c
struct aiocb {
    int             aio_fildes;   // File descriptor
    off_t           aio_offset;   // File offset
    volatile void  *aio_buf;      // Location of buffer
    size_t          aio_nbytes;   // Length of transfer
};
int aio_read(struct aiocb *aiocbp);         // I/O 발행, 성공하면 바로 리턴
int aio_error(const struct aiocb *aiocbp);  // 완료면 0, 아직이면 EINPROGRESS
```

사용 순서: aiocb 를 채움(fd, offset, 길이, 대상 버퍼) → `aio_read()` (즉시 리턴) → 이벤트 루프가 주기적으로 `aio_error()` 로 완료 확인 (이름이 헷갈리지만 이게 "다 됐어?" 함수다) → 완료되면 `aio_return()` 으로 결과 바이트 수를 회수.

문제: I/O 가 수십~수백 개 나가 있으면 각각을 계속 물어보는 게 고통이다. 그래서 일부 시스템은 **인터럽트 방식**, 즉 UNIX **시그널**로 완료를 알려 준다 (POSIX AIO 의 `aio_sigevent`). 이 "폴링 vs 인터럽트" 고민은 장치에서도 똑같이 나온다 → [Ch.36 I/O 장치](2026-09-30_C36_io_devices.md).

> **ASIDE — UNIX SIGNALS**: 시그널은 프로세스와 소통하는 방법이다. 시그널이 오면 하던 일을 멈추고 **시그널 핸들러**를 실행한 뒤 원래 일로 돌아간다. 이름은 `HUP`, `INT`, `SEGV` 등. 커널이 보내기도 한다 (세그폴트 → `SIGSEGV`). 핸들러가 없으면 기본 동작 (SEGV 는 프로세스 종료). 자세한 건 Stevens & Rago [SR05].

원문의 시그널 예제 — `kill -HUP <pid>` 를 보낼 때마다 무한 루프가 잠깐 끊기고 핸들러가 돈다:

```c
#include <stdio.h>
#include <signal.h>
void handle(int arg) { printf("stop wakin' me up...\n"); }
int main(int argc, char *argv[]) {
    signal(SIGHUP, handle);
    while (1)
        ; // doin' nothin' except catchin' some sigs
    return 0;
}
```

```text
prompt> ./main &
[3] 36705
prompt> kill -HUP 36705
stop wakin' me up...
```

(위 출력은 원문 그대로 인용; 이 노트에서 따로 돌리지 않았다.)

비동기 I/O 가 없는 시스템에서는 순수 이벤트 방식이 불가능하다. 대안: **하이브리드** — 네트워크는 이벤트로, 디스크 I/O 는 **스레드 풀**에 맡김 (Flash 웹 서버 [PDZ99]). 오늘날의 node.js(libuv) 도 파일 I/O 는 내부 스레드 풀로 처리한다. 리눅스는 이후 `io_uring`(제출 큐 + 완료 큐 링, 커널과 공유 메모리)으로 디스크·네트워크를 하나의 비동기 인터페이스로 묶었다.

## 8. 또 다른 문제: 상태 관리 (33.7)

스레드 서버에서 "파일 fd 에서 읽어서 소켓 sd 로 보내기":

```c
int rc = read(fd, buffer, size);
rc = write(sd, buffer, size);
```

`read()` 가 리턴하면 어느 소켓에 쓸지 바로 안다 — `sd` 가 **스레드 스택**에 있으니까.

이벤트 서버: `aio_read` 를 날리고 핸들러는 리턴해 버린다. 나중에 `aio_error()` 가 "완료"라고 하면… **이 데이터를 누구에게 보내야 하지?** 스택은 이미 사라졌다. Adya et al. [A+02] 은 이걸 **수동 스택 관리(manual stack management)** 라 부르고, 해법으로 오래된 언어 개념인 **continuation** [FHK84] 을 쓴다: 이벤트를 마저 처리하는 데 필요한 정보를 자료 구조에 기록해 두고, 이벤트가 오면 꺼내 쓴다. 이 예에서는 `fd → sd` 해시 테이블.

```svg
<svg viewBox="0 0 700 280" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs>
    <marker id="C33-arrow2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="170" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">스레드: 상태는 스택에</text>
  <text x="520" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">이벤트: 상태는 continuation 표에</text>
  <line x1="345" y1="30" x2="345" y2="270" stroke="currentColor" stroke-dasharray="3,4"/>
  <rect x="40" y="45" width="260" height="34" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="170" y="67" text-anchor="middle" fill="currentColor">rc = read(fd, buf, size);  ← 여기서 잠듦</text>
  <rect x="40" y="95" width="260" height="34" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="170" y="117" text-anchor="middle" fill="currentColor">rc = write(sd, buf, size);</text>
  <rect x="80" y="160" width="180" height="90" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
  <text x="170" y="180" text-anchor="middle" fill="currentColor">스레드 스택 프레임</text>
  <text x="170" y="202" text-anchor="middle" fill="currentColor">fd = 7</text>
  <text x="170" y="222" text-anchor="middle" fill="currentColor">sd = 12  ← 자동 보존</text>
  <text x="170" y="242" text-anchor="middle" fill="currentColor">buf = 0x...</text>
  <line x1="170" y1="131" x2="170" y2="158" stroke="currentColor" marker-end="url(#C33-arrow2)"/>
  <rect x="370" y="45" width="300" height="34" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="520" y="67" text-anchor="middle" fill="currentColor">① aio_read(&amp;cb) 발행 → 핸들러 리턴</text>
  <rect x="370" y="95" width="300" height="34" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
  <text x="520" y="117" text-anchor="middle" fill="currentColor">② table[fd=7] = { sd=12, cb, buf }  저장</text>
  <rect x="370" y="145" width="300" height="34" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="520" y="167" text-anchor="middle" fill="currentColor">③ … 다른 이벤트 수백 개 처리 …</text>
  <rect x="370" y="195" width="300" height="34" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="520" y="217" text-anchor="middle" fill="currentColor">④ aio_error(cb)==0 → table[7] 조회</text>
  <rect x="370" y="240" width="300" height="30" fill="none" stroke="currentColor"/>
  <text x="520" y="260" text-anchor="middle" fill="currentColor">⑤ write(sd=12, buf) — continuation 실행</text>
  <line x1="520" y1="81" x2="520" y2="93" stroke="currentColor" marker-end="url(#C33-arrow2)"/>
  <line x1="520" y1="131" x2="520" y2="143" stroke="currentColor" marker-end="url(#C33-arrow2)"/>
  <line x1="520" y1="181" x2="520" y2="193" stroke="currentColor" marker-end="url(#C33-arrow2)"/>
  <line x1="520" y1="231" x2="520" y2="238" stroke="currentColor" marker-end="url(#C33-arrow2)"/>
</svg>
```

오늘날의 콜백 + 클로저, Promise/future, `async/await` 코루틴은 모두 **컴파일러/런타임이 continuation 을 대신 만들어 주는** 장치다. `await` 지점에서 지역 변수들이 힙의 상태 객체로 옮겨지는 것 = 자동화된 수동 스택 관리.

## 9. 이벤트 방식에서 여전히 어려운 것 (33.8)

1. **멀티코어**: CPU 여러 개를 쓰려면 핸들러를 병렬로 돌려야 하고, 그러면 임계 구역과 락이 돌아온다. "락 없는 단순함"은 단일 CPU 에서만. (실무 해법: 코어마다 독립 이벤트 루프 + 연결을 코어에 고정 — nginx 워커, `SO_REUSEPORT`.)
2. **페이징 같은 암묵적 블로킹**: 핸들러가 페이지 폴트를 내면 명시적 블로킹 호출이 없어도 블록된다. 흔하면 성능 문제가 크다.
3. **시간에 따른 API 의미 변화**: 어떤 루틴이 논블로킹에서 블로킹으로 바뀌면, 그걸 부르는 핸들러는 둘로 쪼개져야 한다. 블로킹이 치명적이라 모든 API 의 의미 변화를 계속 감시해야 한다.
4. **디스크와 네트워크 I/O 의 통합이 어색함**: `select()` 하나로 모든 I/O 를 관리하고 싶지만, 보통 네트워크는 select, 디스크는 AIO 를 섞어 써야 한다. (FreeBSD kqueue 의 `EVFILT_AIO`, 리눅스 `io_uring` 이 이 간극을 메우려는 시도.)

## 10. 요약 (33.9)

이벤트 서버는 스케줄링 제어권을 애플리케이션에 주지만, 그 대가로 복잡성과 다른 시스템 요소(페이징 등)와의 통합 어려움이 따른다. 그래서 어느 한쪽이 승자가 되지 못했고, **스레드와 이벤트는 같은 문제에 대한 두 접근으로 오래 공존할 것**이다. 관련 논문: [A+02], [PDZ99], [vB+03](Capriccio — 스레드를 극한까지 확장), [WCB01](SEDA — 스레드+큐+이벤트 혼합).

## 11. 직접 해보기

환경: Apple M2 (8코어), macOS 26.4.1, Apple clang 21. 모든 코드 `cc -Wall -Wextra -O0`, **경고 0개**. 리눅스의 `epoll` 대신 macOS 의 `kqueue` 를 썼다 (개념은 같다: 등록과 대기를 분리, 준비된 것만 반환). 이 장에는 OSTEP 숙제 시뮬레이터 폴더가 없다 (`.tools/ostep-homework/` 에 events 관련 디렉터리 없음) — 대신 아래 두 프로그램이 숙제 역할을 한다.

### 11.1 select vs kqueue 이벤트 루프, 그리고 블로킹 핸들러의 대가 (`code/C33_event_loop.c`)

서버(부모, 스레드 1개)와 클라이언트 3개(fork 한 자식 프로세스가 socketpair 3개로 대신 보냄). client i 는 (k×60 + i×10)ms 에 요청 k 를 보낸다. 메시지에 보낸 시각(CLOCK_MONOTONIC — 프로세스 간 공통)을 실어서 서버가 **큐에서 기다린 시간**을 잰다. kqueue 모드는 100ms 주기 `EVFILT_TIMER` 도 같은 루프에서 받는다. 세 번째 인자 `block` 이면 client 0 의 요청 처리 중 `usleep(200ms)` (디스크 읽기를 동기로 했다고 가정).

```c
// C33_event_loop.c — Ch.33 이벤트 루프를 select() 와 kqueue (macOS 의 epoll 대응물) 두 가지로 돌린다.
//  - 서버(부모) 1 스레드, 클라이언트(자식 프로세스 fork) 3개가 socketpair 로 요청을 보낸다.
//  - 요청 메시지에 보낸 시각(CLOCK_MONOTONIC, 프로세스 간 공통)을 넣어 서버가 "대기 지연(latency)" 을 잰다.
//  - 3번째 인자 "block" 을 주면 client 0 의 요청을 처리하는 핸들러가 usleep(200ms) 로 블로킹(디스크 읽기 흉내)
//    → 단일 스레드 이벤트 루프 전체가 멈추고 다른 클라이언트의 지연이 폭증하는 걸 본다.
//  - kqueue 모드에서는 EVFILT_TIMER(100ms 주기) 도 같은 루프에서 받는다.
//
// build: cc -Wall -Wextra -O0 code/C33_event_loop.c -o .work/bin/C33_event_loop
// run  : .work/bin/C33_event_loop select | kqueue [block]
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define NC 3          // 클라이언트 수
#define NREQ 3        // 클라이언트당 요청 수

static long long now_us(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}
static long long T0;
static int blocking_handler = 0;
static long long worst_latency[NC];

// ---------------- 클라이언트 (자식 프로세스) ----------------
static void client_proc(int fds[NC]) {
    // 요청 시각표: client i 는 (k*60 + i*10) ms 에 요청 k 를 보낸다
    for (int k = 0; k < NREQ; k++) {
        for (int i = 0; i < NC; i++) {
            long long due = T0 + (k * 60 + i * 10) * 1000LL;
            long long d = due - now_us();
            if (d > 0) usleep((useconds_t)d);
            char msg[64];
            int n = snprintf(msg, sizeof msg, "%d %d %lld\n", i, k, now_us());
            if (write(fds[i], msg, (size_t)n) != n) _exit(1);
        }
    }
    for (int i = 0; i < NC; i++) close(fds[i]);
    _exit(0);
}

// ---------------- 이벤트 핸들러 ----------------
// 한 fd 에서 읽을 수 있는 만큼 읽고 요청 하나하나 처리. 0 = EOF(닫힘)
static int handle_readable(int fd) {
    char buf[512];
    ssize_t n = read(fd, buf, sizeof buf - 1);        // non-blocking fd
    if (n == 0) return 0;
    if (n < 0) return (errno == EAGAIN) ? 1 : 0;
    buf[n] = 0;
    for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
        int c, k; long long sent;
        if (sscanf(line, "%d %d %lld", &c, &k, &sent) != 3) continue;
        long long lat = now_us() - sent;
        if (lat > worst_latency[c]) worst_latency[c] = lat;
        printf("  t=%4lldms handle client%d req%d  queued %6.1fms\n", (now_us() - T0) / 1000, c, k, lat / 1000.0);
        if (blocking_handler && c == 0) usleep(200000);  // 블로킹 호출! 루프 전체가 같이 멈춘다
    }
    return 1;
}

static void loop_select(int fds[NC]) {
    int open_cnt = NC, alive[NC] = {1, 1, 1};
    while (open_cnt > 0) {
        fd_set rfds; FD_ZERO(&rfds);
        int maxfd = -1;
        for (int i = 0; i < NC; i++) if (alive[i]) { FD_SET(fds[i], &rfds); if (fds[i] > maxfd) maxfd = fds[i]; }
        int rc = select(maxfd + 1, &rfds, NULL, NULL, NULL);      // 매번 집합 전체를 다시 넘긴다
        if (rc < 0) { perror("select"); exit(1); }
        for (int i = 0; i < NC; i++)
            if (alive[i] && FD_ISSET(fds[i], &rfds) && !handle_readable(fds[i])) {
                alive[i] = 0; open_cnt--; close(fds[i]);
            }
    }
}

static void loop_kqueue(int fds[NC]) {
    int kq = kqueue();
    struct kevent ch[NC + 1];
    for (int i = 0; i < NC; i++) EV_SET(&ch[i], fds[i], EVFILT_READ, EV_ADD, 0, 0, (void *)(long)i);
    EV_SET(&ch[NC], 1, EVFILT_TIMER, EV_ADD, 0, 100, NULL);       // 100ms 주기 타이머 (ident=1)
    if (kevent(kq, ch, NC + 1, NULL, 0, NULL) < 0) { perror("kevent add"); exit(1); }  // 등록은 한 번만
    int open_cnt = NC, ticks = 0;
    while (open_cnt > 0) {
        struct kevent ev[8];
        int n = kevent(kq, NULL, 0, ev, 8, NULL);                 // 준비된 것만 돌려받는다
        if (n < 0) { perror("kevent"); exit(1); }
        for (int j = 0; j < n; j++) {
            if (ev[j].filter == EVFILT_TIMER) {
                ticks++;
                printf("  t=%4lldms timer tick #%d (fired %ld time(s))\n", (now_us() - T0) / 1000, ticks, (long)ev[j].data);
                continue;
            }
            int fd = (int)ev[j].ident;
            if (!handle_readable(fd) || ((ev[j].flags & EV_EOF) && ev[j].data == 0)) {
                close(fd); open_cnt--;                          // close 하면 kqueue 등록도 자동 해제
            }
        }
    }
    close(kq);
}

int main(int argc, char *argv[]) {
    const char *api = argc > 1 ? argv[1] : "kqueue";
    blocking_handler = (argc > 2 && strcmp(argv[2], "block") == 0);
    setvbuf(stdout, NULL, _IOLBF, 0);

    int srv[NC], cli[NC];
    for (int i = 0; i < NC; i++) {
        int sv[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) { perror("socketpair"); return 1; }
        srv[i] = sv[0]; cli[i] = sv[1];
        fcntl(srv[i], F_SETFL, fcntl(srv[i], F_GETFL) | O_NONBLOCK);
    }
    T0 = now_us() + 20000;                    // 20ms 뒤부터 요청 시작
    pid_t pid = fork();
    if (pid == 0) { for (int i = 0; i < NC; i++) close(srv[i]); client_proc(cli); }
    for (int i = 0; i < NC; i++) close(cli[i]);

    printf("api=%s handler=%s\n", api, blocking_handler ? "BLOCKING (client0 sleeps 200ms)" : "non-blocking");
    if (strcmp(api, "select") == 0) loop_select(srv); else loop_kqueue(srv);
    waitpid(pid, NULL, 0);
    printf("worst queueing latency:");
    for (int i = 0; i < NC; i++) printf(" client%d=%.1fms", i, worst_latency[i] / 1000.0);
    printf("\n");
    return 0;
}
```

```text
$ cc -Wall -Wextra -O0 code/C33_event_loop.c -o .work/bin/C33_event_loop
$ .work/bin/C33_event_loop select
api=select handler=non-blocking
  t=   2ms handle client0 req0  queued    0.0ms
  t=  12ms handle client1 req0  queued    0.0ms
  t=  22ms handle client2 req0  queued    0.0ms
  t=  62ms handle client0 req1  queued    0.0ms
  t=  72ms handle client1 req1  queued    0.0ms
  t=  82ms handle client2 req1  queued    0.0ms
  t= 121ms handle client0 req2  queued    0.0ms
  t= 132ms handle client1 req2  queued    0.0ms
  t= 141ms handle client2 req2  queued    0.0ms
worst queueing latency: client0=0.0ms client1=0.0ms client2=0.0ms
$ .work/bin/C33_event_loop kqueue
api=kqueue handler=non-blocking
  t=   2ms handle client0 req0  queued    0.0ms
  t=  12ms handle client1 req0  queued    0.1ms
  t=  20ms handle client2 req0  queued    0.1ms
  t=  62ms handle client0 req1  queued    0.1ms
  t=  72ms handle client1 req1  queued    0.0ms
  t=  82ms handle client2 req1  queued    0.1ms
  t=  88ms timer tick #1 (fired 1 time(s))
  t= 120ms handle client0 req2  queued    0.1ms
  t= 132ms handle client1 req2  queued    0.1ms
  t= 141ms handle client2 req2  queued    0.1ms
worst queueing latency: client0=0.1ms client1=0.1ms client2=0.1ms
$ .work/bin/C33_event_loop kqueue block
api=kqueue handler=BLOCKING (client0 sleeps 200ms)
  t=   2ms handle client0 req0  queued    0.1ms
  t= 212ms handle client0 req1  queued  151.7ms
  t= 422ms handle client0 req2  queued  300.6ms
  t= 632ms handle client1 req0  queued  620.7ms
  t= 632ms handle client1 req1  queued  560.7ms
  t= 632ms handle client1 req2  queued  500.8ms
  t= 632ms handle client2 req0  queued  612.1ms
  t= 632ms handle client2 req1  queued  550.8ms
  t= 632ms handle client2 req2  queued  490.8ms
  t= 632ms timer tick #1 (fired 2 time(s))
  t= 632ms timer tick #2 (fired 4 time(s))
worst queueing latency: client0=300.6ms client1=620.7ms client2=612.1ms
```

해석:

- **select / kqueue (논블로킹)**: 모든 요청이 도착 0.0~0.2ms 안에 처리됐다. 스레드 하나로 세 클라이언트를 "동시에" 서비스한다. 기능은 같고 차이는 API 형태: select 는 매 반복마다 `FD_ZERO/FD_SET` 으로 집합을 새로 만들어 넘기고, kqueue 는 처음에 `EV_ADD` 로 한 번 등록한 뒤 `kevent()` 가 준비된 것만 배열로 돌려준다. 타이머도 fd 와 같은 큐로 들어온다 (`EVFILT_TIMER`).
- **블로킹 핸들러 한 개의 대가**: client 0 의 요청을 처리하며 200ms 잠들자, **client1·2 의 요청은 최대 620.7ms / 612.1ms 동안 큐에서 기다렸다** (논블로킹일 때 0.1ms). client 0 자신도 req1, req2 가 151.7ms, 300.6ms 를 기다렸다. 게다가 client0 의 소켓에 req1, req2 가 함께 쌓여 있어 핸들러가 한 번의 read 로 둘 다 처리하며 **연달아 400ms** 를 블록했다 — 한 핸들러가 루프를 독점하는 **head-of-line blocking**. 타이머도 그동안 처리되지 못해 kqueue 가 만료 횟수를 합쳐서(`fired 2`, `fired 4` = 그 사이 쌓인 만료 수) 한꺼번에 전달했다.

### 11.2 POSIX AIO + continuation (`code/C33_aio_continuation.c`)

4MB 파일을 1MB 씩 4개의 `aio_read()` 로 **뒤섞인 순서(3,1,0,2)** 로 한꺼번에 발행하고, 각 요청에 continuation `{aiocb, 보낼 소켓 sd, req_id}` 를 붙인다. 루프는 `aio_error()` 로 완료를 폴링하면서 그 사이 "다른 일" 반복 횟수를 센다. 완료되면 continuation 에서 sd 를 찾아 응답(앞 16바이트)을 보내고, 마지막에 클라이언트 쪽에서 맞는 데이터를 받았는지 확인한다.

```c
// C33_aio_continuation.c — Ch.33.6~33.7 비동기 I/O(POSIX AIO, macOS 지원) + continuation(수동 스택 관리).
//  - 4MB 임시 파일을 만들고, 1MB 씩 4개의 aio_read() 를 "한꺼번에" 날린다 (즉시 리턴).
//  - 각 요청마다 continuation = { aiocb, 끝나면 써 줄 소켓(sd), 요청 id } 를 표에 저장한다.
//    스레드 버전이라면 read(fd); write(sd); 두 줄이면 끝 — sd 는 스택에 있으니까.
//    이벤트 버전에서는 read 가 끝났을 때 "누구에게 보내야 하지?" 를 continuation 에서 찾아야 한다.
//  - 이벤트 루프는 aio_error() 로 완료를 폴링하면서, 그 사이 "다른 일" 을 몇 번 했는지 센다.
//
// build: cc -Wall -Wextra -O0 code/C33_aio_continuation.c -o .work/bin/C33_aio_continuation
#include <aio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define CHUNK (1 << 20)
#define NREQ 4

typedef struct {               // continuation: I/O 가 끝난 뒤 이어서 할 일에 필요한 상태
    struct aiocb cb;
    int sd;                    // 결과를 보낼 "네트워크 소켓"
    int req_id;
    int done;
} cont_t;

int main(void) {
    char path[] = "/tmp/c33_aio_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) { perror("mkstemp"); return 1; }
    char *data = malloc(CHUNK);
    for (int i = 0; i < NREQ; i++) {                 // 청크 i 는 전부 'A'+i
        memset(data, 'A' + i, CHUNK);
        if (write(fd, data, CHUNK) != CHUNK) { perror("write"); return 1; }
    }
    fsync(fd);

    // 클라이언트 소켓 4개 (socketpair 의 한쪽 = 서버가 쓸 sd, 다른 쪽 = 클라이언트가 읽음)
    int client_end[NREQ];
    cont_t *conts = calloc(NREQ, sizeof(cont_t));
    for (int i = 0; i < NREQ; i++) {
        int sv[2];
        socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
        conts[i].sd = sv[0]; client_end[i] = sv[1];
    }

    // 1) 비동기 읽기 4개를 한꺼번에 발행 — 요청 순서를 일부러 뒤섞음 (3,1,0,2)
    int order[NREQ] = {3, 1, 0, 2};
    for (int j = 0; j < NREQ; j++) {
        int i = order[j];
        cont_t *c = &conts[i];
        c->req_id = i;
        c->cb.aio_fildes = fd;
        c->cb.aio_offset = (off_t)i * CHUNK;
        c->cb.aio_buf = malloc(CHUNK);
        c->cb.aio_nbytes = CHUNK;
        if (aio_read(&c->cb) != 0) { perror("aio_read"); return 1; }
        printf("issued aio_read req%d (offset %dMB) -> returned immediately\n", i, i);
    }

    // 2) 이벤트 루프: 완료 폴링 + 그 사이 다른 일
    long other_work = 0, polls = 0;
    int remaining = NREQ;
    while (remaining > 0) {
        for (int i = 0; i < NREQ; i++) {
            cont_t *c = &conts[i];
            if (c->done) continue;
            polls++;
            int e = aio_error(&c->cb);
            if (e == EINPROGRESS) continue;
            if (e != 0) { fprintf(stderr, "req%d failed: %s\n", i, strerror(e)); return 1; }
            ssize_t n = aio_return(&c->cb);          // 완료된 요청은 반드시 aio_return 으로 회수
            // continuation 실행: 저장해 둔 sd 로 "응답" 의 첫 16바이트를 보낸다
            char *buf = (char *)c->cb.aio_buf;
            ssize_t w = write(c->sd, buf, 16);
            printf("req%d complete: %zd bytes, first byte '%c' -> continuation writes %zd bytes to sd=%d\n",
                   c->req_id, n, buf[0], w, c->sd);
            c->done = 1; remaining--;
        }
        other_work++;                                // 여기서 네트워크 이벤트 처리 등 다른 일을 할 수 있다
    }
    printf("loop iterations doing other work: %ld, aio_error polls: %ld\n", other_work, polls);

    // 3) 클라이언트 쪽에서 받은 것 확인
    for (int i = 0; i < NREQ; i++) {
        char got[17] = {0};
        ssize_t r = read(client_end[i], got, 16);
        printf("client%d received %zd bytes: %.16s\n", i, r, got);
    }
    unlink(path);
    return 0;
}
```

```text
$ cc -Wall -Wextra -O0 code/C33_aio_continuation.c -o .work/bin/C33_aio_continuation && .work/bin/C33_aio_continuation
issued aio_read req3 (offset 3MB) -> returned immediately
issued aio_read req1 (offset 1MB) -> returned immediately
issued aio_read req0 (offset 0MB) -> returned immediately
issued aio_read req2 (offset 2MB) -> returned immediately
req3 complete: 1048576 bytes, first byte 'D' -> continuation writes 16 bytes to sd=10
req1 complete: 1048576 bytes, first byte 'B' -> continuation writes 16 bytes to sd=6
req0 complete: 1048576 bytes, first byte 'A' -> continuation writes 16 bytes to sd=4
req2 complete: 1048576 bytes, first byte 'C' -> continuation writes 16 bytes to sd=8
loop iterations doing other work: 664, aio_error polls: 1548
client0 received 16 bytes: AAAAAAAAAAAAAAAA
client1 received 16 bytes: BBBBBBBBBBBBBBBB
client2 received 16 bytes: CCCCCCCCCCCCCCCC
client3 received 16 bytes: DDDDDDDDDDDDDDDD
```

해석:

- 4개의 `aio_read` 가 전부 **즉시 리턴**했고, 완료 순서(3,1,0,2)는 발행 순서와 무관했다 (실행마다 바뀐다; 다른 실행에서는 1,3,0,2). 완료를 기다리는 동안 루프는 **664번** 다른 일을 할 기회가 있었다 — 블로킹 `read()` 였다면 0번.
- 각 완료는 **continuation 에 저장된 sd** 로 정확히 배달됐다: client0 은 'A'(0MB 청크), client3 은 'D'. 완료 순서가 뒤섞여도 맞는 곳으로 간다는 게 핵심이다. 스레드였다면 이 매핑은 그냥 지역 변수였다.
- 정직한 주석: 방금 쓴 파일이라 데이터가 페이지 캐시에 있어서 실제 디스크 I/O 는 거의 없었다. 그래도 API 흐름(발행 → 폴링 → `aio_return` → continuation)은 진짜 디스크일 때와 같다. 폴링 1,548번은 Ch.36 의 "폴링 vs 인터럽트" 트레이드오프를 그대로 보여 준다 — 완료가 드물면 시그널/`EVFILT_AIO` 같은 통지 방식이 낫다.

## 12. 펌웨어 엔지니어의 눈으로

- **베어메탈 슈퍼루프(superloop) = 이벤트 루프.** `while(1) { if (flags & EV_UART) handle_uart(); if (flags & EV_DMA_DONE) handle_dma(); ... __WFI(); }` — ISR 은 플래그만 세우고(= 커널이 ready list 에 올림), 메인 루프가 하나씩 처리한다. "핸들러에서 블록 금지"는 펌웨어에서 "메인 루프에서 busy-wait 금지, 긴 작업은 상태 머신으로 쪼개기"와 같다. 메인 루프 최악 반복 시간이 곧 이벤트 지연 상한이다 (11.1 의 620ms 와 같은 계산).
- **FW 상태 머신 = continuation.** SSD FW 에서 NAND read 를 발행하고 완료 인터럽트를 기다리는 동안, "이 read 가 끝나면 어느 NVMe 커맨드의 어느 PRP 로 DMA 해야 하는지" 를 **커맨드 컨텍스트(slot) 구조체**에 저장해 둔다. 완료 시 태그(CID/slot id)로 컨텍스트를 찾아 다음 단계로 진행 — 11.2 의 `table[fd] → sd` 와 정확히 같은 구조. RTOS 태스크로 짜면 이 컨텍스트가 태스크 스택에 자동으로 있다 (스레드 방식).
- **NVMe 와 io_uring 은 같은 설계다.** 제출 큐(SQ)에 요청을 넣고 doorbell, 완료 큐(CQ)를 폴링하거나 인터럽트(MSI-X)로 통지받는다. `aio_read` → `aio_error` 폴링 → 시그널 통지의 진화가 하드웨어 큐 인터페이스와 수렴했다. 고성능 경로(SPDK, io_uring SQPOLL)는 인터럽트 대신 **폴링 모드**를 쓴다 — 완료가 충분히 잦으면 폴링이 인터럽트보다 싸다는 Ch.36 의 결론.
- **인터럽트 coalescing = kqueue 타이머 이벤트 합치기.** 11.1 에서 루프가 늦자 kqueue 는 타이머 만료를 따로 여러 번 주지 않고 `data` 필드에 횟수를 담아 한 번에 줬다. NVMe 의 interrupt coalescing(aggregation threshold/time)이나 NIC 의 NAPI 도 같은 원리: 이벤트를 합쳐 처리 오버헤드를 줄이고, 대신 개별 이벤트의 지연을 조금 늘린다.
- **AI 가속기 호스트 런타임**: 커맨드 버퍼 제출은 비동기(즉시 리턴)이고, 완료는 fence/event 로 확인한다. 런타임 스레드 하나가 여러 스트림의 완료를 `kqueue`/`epoll` 이나 드라이버 이벤트 fd 로 기다리는 구조가 흔하다. "핸들러에서 동기 `cudaDeviceSynchronize()` 하지 마라" 가 이 장의 TIP 그대로다.

## 13. 면접 질문

### Q1. select, poll, epoll/kqueue 의 차이는? 연결이 많을 때 무엇을 쓰나?
<details>
<summary>답 보기</summary>

**select**: fd 비트맵을 매 호출 넘기고 결과로 덮어씀 → 매번 재구성, 커널·사용자 모두 O(최대 fd) 스캔, `FD_SETSIZE`(보통 1024) 제한. **poll**: `pollfd` 배열이라 fd 수 제한은 없지만 여전히 매 호출 전체 전달 + O(n) 스캔. **epoll/kqueue**: 관심 fd 를 커널 객체에 **한 번 등록**하고, 대기 호출은 **준비된 것만** 돌려줌 → O(준비된 수). kqueue 는 타이머, 시그널, 프로세스 종료, 파일 변경, AIO 까지 같은 큐로 받는 범용 이벤트 인터페이스. 연결 1만 개에 활성 10개라면 select 는 호출당 ~2만 번 검사, epoll/kqueue 는 10개. 대량 연결이면 epoll/kqueue (또는 io_uring).

</details>

### Q2. level-triggered 와 edge-triggered 의 차이는? edge-triggered 에서 주의할 점은?
<details>
<summary>답 보기</summary>

**Level-triggered**(select/poll 과 epoll·kqueue 기본): 읽을 데이터가 **남아 있는 동안** 매번 "준비됨"을 알려 준다. 덜 읽어도 다음에 또 알려 주니 안전. **Edge-triggered**(`EPOLLET`, kqueue `EV_CLEAR`): 상태가 **바뀔 때 한 번**만 알려 준다. 그래서 반드시 fd 를 **논블로킹**으로 두고 `EAGAIN` 이 나올 때까지 **끝까지 읽어야** 한다 — 덜 읽으면 새 데이터가 오기 전까지 다시는 안 깨운다(사실상 lost wakeup). 이점은 같은 이벤트의 반복 통지가 줄고 멀티스레드에서 thundering herd 를 줄이기 쉬움.

</details>

### Q3. 이벤트 기반 서버에서 디스크 I/O 나 CPU 무거운 작업은 어떻게 처리하나?
<details>
<summary>답 보기</summary>

이벤트 루프는 **절대 블록하면 안 되므로**: (1) **비동기 I/O** — POSIX AIO, 리눅스 `io_uring`, Windows IOCP. 완료를 이벤트로 받아 continuation 실행. (2) **스레드 풀 하이브리드** — 블로킹 작업을 워커 스레드에 넘기고 완료를 pipe/eventfd 로 루프에 알림 (Flash [PDZ99], node.js libuv 의 파일 I/O, nginx thread pool). (3) CPU 무거운 작업도 풀로 보내거나 작은 조각으로 나눠 다른 이벤트 사이에 끼워 넣기. 실측: 핸들러 하나가 200ms 블록하자 다른 클라이언트 대기 지연이 0.1ms → 620ms 로 증가.

</details>

### Q4. 스레드 기반과 이벤트 기반 서버의 장단점을 비교하라.
<details>
<summary>답 보기</summary>

**스레드**: 코드가 순차적이라 읽기 쉽고 상태가 스택에 자동 보존, 블로킹 I/O 자연스럽게 겹침, 멀티코어 활용 쉬움. 대신 락·교착·경쟁 조건, 스레드당 스택 메모리, 문맥 교환 비용, 스케줄링을 OS 에 맡김. **이벤트**: 단일 코어에서 락 불필요, 연결당 메모리가 작아 C10K 에 유리, 스케줄링을 앱이 제어. 대신 **블로킹 금지**, continuation/콜백으로 인한 **수동 스택 관리**(콜백 지옥), 페이지 폴트 같은 암묵적 블로킹에 취약, 멀티코어에서는 결국 락이나 코어별 루프 필요. 현실은 혼합: 코어당 이벤트 루프 + 블로킹 작업용 스레드 풀, 그리고 async/await 로 이벤트 방식을 스레드처럼 쓰기.

</details>

### Q5. continuation 이란 무엇이고, async/await 와는 어떤 관계인가?
<details>
<summary>답 보기</summary>

비동기 연산이 끝난 뒤 **이어서 실행할 계산과 그에 필요한 상태**를 묶어 저장한 것. 이벤트 서버에서는 `aio_read` 를 발행하며 "끝나면 이 sd 로 보내라"를 `fd → {sd, buf}` 표에 넣고, 완료 이벤트에서 꺼내 실행한다 (Adya 의 manual stack management). **async/await** 는 컴파일러가 함수를 상태 머신으로 변환해, `await` 지점에서 살아 있는 지역 변수를 힙 객체에 옮기고 나머지 코드를 continuation 으로 등록하는 것 — 즉 continuation 을 **자동으로** 만들어 준다. 프로그래머는 스레드처럼 순차 코드를 쓰고 런타임은 이벤트 방식으로 돈다.

</details>

## 14. 자가 점검 & 숙제

### 퀴즈 1. select 의 fd_set 을 왜 매 반복마다 FD_ZERO/FD_SET 으로 다시 만들어야 하나?
<details>
<summary>정답</summary>

select 가 리턴할 때 집합을 **준비된 fd 만 남은 부분집합으로 덮어쓰기** 때문이다. 다시 안 만들면 다음 호출에서 지난번에 준비 안 됐던 fd 들은 아예 감시 대상에서 빠진다. (kqueue/epoll 은 등록이 커널에 유지되므로 매번 다시 넘길 필요가 없다.)

</details>

### 퀴즈 2. 단일 스레드 이벤트 서버에서 정말로 락이 전혀 필요 없나? 두 가지 예외를 들어라.
<details>
<summary>정답</summary>

(1) **멀티코어** 에서 핸들러를 병렬로 돌리면 공유 상태에 락이 필요하다. (2) **시그널 핸들러** 나 스레드 풀(하이브리드) 처럼 루프 밖의 실행 흐름이 같은 상태를 건드리면 동기화가 필요하다 (시그널 핸들러에서는 async-signal-safe 함수만, 보통 플래그/pipe 로 루프에 알림).

</details>

### 퀴즈 3. 11.1 의 블로킹 실험에서 client1 의 req0 이 620.7ms 나 기다린 이유를 시간순으로 설명하라.
<details>
<summary>정답</summary>

client1 req0 은 t≈10ms 에 도착했다. 그 시점 루프는 client0 req0 핸들러 안에서 200ms 잠들어 있었다 (t≈2~202). 깨어난 뒤 다음 kevent 는 client0 의 소켓을 먼저 돌려줬고, 거기엔 req1(60ms)과 req2(120ms)가 함께 쌓여 있어 핸들러가 둘을 연달아 처리하며 또 400ms 블록했다 (t≈212~622). 그 후에야 client1 의 이벤트가 처리돼 t≈632ms. 632 − 10 ≈ 620ms.

</details>

### 퀴즈 4. "블로킹 호출을 하나도 안 썼는데" 이벤트 서버가 멈출 수 있는 경우는?
<details>
<summary>정답</summary>

**페이지 폴트** (핸들러 코드나 데이터가 스왑돼 있음 → 디스크 읽기 동안 블록), 메모리 할당이 커널 회수(reclaim)에 들어가는 경우, `mmap` 된 파일 접근, 또는 라이브러리 함수가 내부적으로 블로킹(DNS 조회 `getaddrinfo`, 로깅이 디스크 fsync 등). 이 장이 말한 "implicit blocking".

</details>

### 숙제 (이 장은 v0.91 에 문항과 숙제 시뮬레이터가 없어 직접 만든 과제)

- `C33_event_loop.c` 에 `EVFILT_WRITE` 를 추가해, 응답을 소켓 송신 버퍼가 찰 때까지 쓰고 `EAGAIN` 이면 쓰기 준비 이벤트를 기다리게 바꾸기 → **"쓰기도 블록할 수 있다"** 와 쓰기 준비 이벤트의 쓰임 확인.
- 블로킹 `usleep(200ms)` 를 "워커 스레드에 넘기고 완료를 pipe 로 루프에 알리기"로 바꿔서 client1·2 의 지연이 다시 ~0ms 가 되는지 측정 → **하이브리드(Flash 방식)** 체험.
- `C33_aio_continuation.c` 에서 `aio_sigevent.sigev_notify = SIGEV_SIGNAL` 로 완료를 시그널로 받게 하고, 폴링 횟수가 어떻게 바뀌는지 비교 → **폴링 vs 인터럽트**.

## 15. 다음으로

- 동시성 파트 끝. 다음은 영속성(persistence): [Ch.36 I/O 장치](2026-09-30_C36_io_devices.md) — 폴링 vs 인터럽트, DMA 가 이 장의 AIO·이벤트 통지와 직접 이어진다.
- 스레드 쪽 해법 복습: [Ch.30 조건 변수](2026-09-30_C30_condition_variables.md), [Ch.32 흔한 동시성 버그](2026-09-30_C32_concurrency_bugs.md).
