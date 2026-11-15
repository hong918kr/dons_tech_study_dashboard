---
id: OS-M12
time: 90분
updated: 2026-11-11
---
# OS-M12 I/O 모델 비교: blocking·non-blocking·select·kqueue 에코 서버

## 학습 목표

- 같은 TCP 에코 서버를 **연결당 스레드(blocking)**, **논블로킹 + select**, **kqueue 이벤트 루프** 세 가지로 만든다. 각 모델에서 "연결 상태가 어디에 사는가"를 설명할 수 있다. 스레드 모델에서는 스레드 스택에, 이벤트 모델에서는 `conn_t` 구조체에 산다.
- 소켓 버퍼보다 큰 메시지(300000 B)로 **짧은 쓰기(short write)와 EAGAIN**을 일부러 만든다. 남은 출력을 들고 쓰기 가능 알림을 기다리는 상태기계를 만들고, 그동안 읽기를 멈추는 **역압(backpressure)**까지 구현한다.
- select 의 `FD_SETSIZE = 1024` 한계를 실제로 부딪혀 본다. fd 0..1023 을 채운 뒤 들어온 연결은 가드가 거절해야 한다. 가드 없이 `FD_SET(1050)` 을 하면 이 맥에서 무슨 일이 나는지도 직접 관찰한다.
- kqueue 의 레벨 모드와 `EV_CLEAR` 를 테스트로 구분한다. 읽지 않고 `kevent` 를 다시 부르면 레벨 모드는 이벤트 1개, EV_CLEAR 는 0개다. EV_CLEAR 에서는 왜 EAGAIN 까지 비워야 하는지도 설명한다.
- 1000 연결 부하 생성기로 처리량 · p50/p99 RTT · 서버 RSS · 스레드 수 · CPU 를 잰다. 숫자로 말한다: "1000 연결에서 연결당 메모리 32 KB vs 16 KB, 1000 개 중 10 개만 바쁠 때 select 처리량은 kqueue 의 절반."

## 1. 문제

"동시 연결 1만 개를 받는 서버를 어떻게 짜겠어요? 스레드? select? epoll/kqueue? 각각 뭐가 문제죠?" 이른바 C10K 질문은 시스템 면접의 단골이다. 임베디드 리눅스에서도 그대로 나온다. 게이트웨이 데몬은 센서 수십 개의 TCP/Unix 소켓, UART, 타이머를 한 프로세스에서 다룬다. RAM 이 64~256 MB 인 보드에서 연결마다 스레드를 띄우면 스택과 커널 스레드 구조체가 먼저 바닥난다. MCU 쪽 lwIP 에도 같은 갈림길이 있다. 블로킹 netconn/socket API(태스크당 연결)와 콜백 raw API(이벤트) 중 하나를 골라야 한다.

OSTEP 33장(Event-based Concurrency)이 select 기반 이벤트 루프와 그 어려움을 다룬다. 이벤트 처리기는 절대 블록하면 안 되고(33.5), 상태를 직접 들고 다녀야 한다(33.7). 이 레슨은 그 두 문장을 코드로 확인한다. 스레드 기초는 OS-B04, 문맥 교환 비용은 OS-M02, kqueue 데몬 전체 구조(워커 풀 · 시그널 · 재시작)는 OS-A11 이 다룬다. 리눅스 epoll 의 LT/ET 는 OS-M13 의 몫이다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<defs><marker id="m12a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" fill="currentColor" font-weight="bold">(1) 연결당 스레드</text>
<text x="255" y="20" fill="currentColor" font-weight="bold">(2) 논블로킹 + select</text>
<text x="505" y="20" fill="currentColor" font-weight="bold">(3) kqueue 이벤트 루프</text>
<line x1="245" y1="8" x2="245" y2="320" stroke="currentColor" stroke-dasharray="2 4"/>
<line x1="495" y1="8" x2="495" y2="320" stroke="currentColor" stroke-dasharray="2 4"/>
<rect x="10" y="34" width="70" height="26" rx="3" class="f-mut" stroke="currentColor"/><text x="16" y="52" fill="currentColor" font-size="12">acceptor</text>
<rect x="10" y="70" width="70" height="26" rx="3" class="f-warn" stroke="currentColor"/><text x="16" y="88" fill="currentColor" font-size="12">T1 read()</text>
<rect x="88" y="70" width="70" height="26" rx="3" class="f-warn" stroke="currentColor"/><text x="94" y="88" fill="currentColor" font-size="12">T2 read()</text>
<rect x="166" y="70" width="70" height="26" rx="3" class="f-warn" stroke="currentColor"/><text x="170" y="88" fill="currentColor" font-size="12">… T1000</text>
<text x="10" y="118" fill="currentColor" font-size="12">각자 블록 = 커널이 깨워 줌</text>
<text x="10" y="134" fill="currentColor" font-size="12">상태 = 스레드 스택 (buf, fd)</text>
<rect x="10" y="150" width="226" height="34" rx="3" class="f-mut" stroke="currentColor"/>
<text x="18" y="171" fill="currentColor" font-size="12">커널: 소켓마다 대기 큐 1개</text>
<line x1="45" y1="96" x2="45" y2="150" stroke="currentColor" marker-end="url(#m12a)"/>
<line x1="123" y1="96" x2="123" y2="150" stroke="currentColor" marker-end="url(#m12a)"/>
<line x1="201" y1="96" x2="201" y2="150" stroke="currentColor" marker-end="url(#m12a)"/>
<text x="10" y="210" fill="currentColor" font-size="12">1000 연결 실측: 스레드 1002 개</text>
<text x="10" y="226" fill="currentColor" font-size="12">RSS 32.1 KB/연결 (스택 페이지)</text>
<text x="10" y="242" fill="currentColor" font-size="12">자발적 문맥 교환 ≈ 메시지 수</text>
<rect x="255" y="34" width="230" height="26" rx="3" class="f-acc" stroke="currentColor"/><text x="262" y="52" fill="currentColor" font-size="12">루프 스레드 1개 + conn_t 목록</text>
<line x1="300" y1="60" x2="300" y2="150" stroke="currentColor" marker-end="url(#m12a)"/>
<text x="306" y="92" fill="currentColor" font-size="12">매번 fd_set 비트맵(128 B)을</text>
<text x="306" y="108" fill="currentColor" font-size="12">새로 만들어 통째로 복사</text>
<line x1="440" y1="150" x2="440" y2="60" stroke="currentColor" marker-end="url(#m12a)"/>
<text x="350" y="134" fill="currentColor" font-size="12">준비 비트맵 반환</text>
<rect x="255" y="150" width="230" height="34" rx="3" class="f-mut" stroke="currentColor"/>
<text x="262" y="171" fill="currentColor" font-size="12">커널: 0..maxfd 전부 검사</text>
<text x="255" y="210" fill="currentColor" font-size="12">앱도 준비된 fd 를 찾으려고 O(n) 순회</text>
<text x="255" y="226" fill="currentColor" font-size="12">fd ≥ FD_SETSIZE(1024) 는 넣을 수 없음</text>
<text x="255" y="242" fill="currentColor" font-size="12">10/1000 활성 실측: 처리량 절반</text>
<rect x="505" y="34" width="228" height="26" rx="3" class="f-acc" stroke="currentColor"/><text x="512" y="52" fill="currentColor" font-size="12">루프 스레드 1개, udata = conn_t*</text>
<line x1="540" y1="60" x2="540" y2="150" stroke="currentColor" marker-end="url(#m12a)"/>
<text x="546" y="92" fill="currentColor" font-size="12">관심 등록은 바뀔 때만</text>
<text x="546" y="108" fill="currentColor" font-size="12">(EV_ADD / ENABLE / DISABLE)</text>
<line x1="700" y1="150" x2="700" y2="60" stroke="currentColor" marker-end="url(#m12a)"/>
<text x="590" y="134" fill="currentColor" font-size="12">준비된 것만 반환</text>
<rect x="505" y="150" width="228" height="34" rx="3" class="f-ok" stroke="currentColor"/>
<text x="512" y="171" fill="currentColor" font-size="12">커널: knote 가 준비되면 큐에 넣음</text>
<text x="505" y="210" fill="currentColor" font-size="12">비용 ∝ 준비된 이벤트 수 (등록 수 아님)</text>
<text x="505" y="226" fill="currentColor" font-size="12">fd 상한 = RLIMIT_NOFILE</text>
<text x="505" y="242" fill="currentColor" font-size="12">리눅스 대응: epoll (OS-M13)</text>
<rect x="10" y="262" width="723" height="56" rx="4" class="f-mut" stroke="currentColor"/>
<text x="20" y="282" fill="currentColor" font-size="12">공통 규칙: 이벤트 모델의 처리기는 절대 블록하지 않는다 (OSTEP 33.5). 그래서 fd 는 O_NONBLOCK 이고,</text>
<text x="20" y="300" fill="currentColor" font-size="12">write 가 EAGAIN 이면 남은 바이트를 conn_t 에 들고 "쓰기 가능" 알림을 기다린다. 스레드 모델은 그냥 write 에서 잔다.</text>
</svg>
```

| | 연결당 스레드 | select | kqueue |
|---|---|---|---|
| 연결 상태 위치 | 스레드 스택 (지역 변수) | `conn_t` (힙) | `conn_t` (힙), `udata` 로 바로 찾음 |
| 기다리는 곳 | 연결마다 `read`/`write` | `select` 한 곳 | `kevent` 한 곳 |
| 매 대기 비용 | 없음 (스레드 하나가 소켓 하나) | 집합 재구성 + 커널 검사 O(maxfd) + 앱 순회 O(n) | O(준비된 이벤트) + 관심이 바뀔 때만 등록 |
| 상한 | 스레드 수 · 메모리 (`kern.num_taskthreads` = 8192) | `FD_SETSIZE` = 1024 (맥 · 리눅스 glibc 둘 다) | `RLIMIT_NOFILE` (이 맥 `kern.maxfilesperproc` = 122880) |
| 동기화 | 공유 상태마다 락 필요 | 루프 안은 락 불필요 | 루프 안은 락 불필요 |
| 블로킹 호출 (디스크 · DNS) | 그 스레드만 멈춤 | 서버 전체가 멈춤 | 서버 전체가 멈춤 → 워커 풀 (OS-A11) |

### Part A — 연결당 스레드 (blocking)

- 하네스가 주어진다. acceptor 스레드는 `poll(listen, wake)` 로 기다렸다가 `accept` 하고, 연결마다 `pthread_create` 한다. 정지할 때는 살아 있는 연결 fd 를 `shutdown(SHUT_RDWR)` 해서 블록된 스레드를 깨운 뒤 전부 join 한다.
- **TODO A1** `write_all(fd, p, n, st)`: n 바이트를 다 쓸 때까지 `write` 를 반복한다. `EINTR` 이면 다시 하고, 짧은 쓰기면 남은 부분부터 이어서 쓴다. 통계로 `writes`, `short_writes`, `bytes_out` 을 센다.
- **TODO A2** `thread_conn_loop(fd, st)`: EOF(0) 나 오류가 날 때까지 `read` → `write_all` 을 돈다.
- 검증 방법: 테스트는 300000 B 를 4 KB `SO_SNDBUF` socketpair 로 보낸다. 받는 쪽은 fork 한 자식이 천천히 읽는다. 이어서 클라이언트 64 개가 동시에 붙어 다섯 크기 메시지를 바이트 단위로 비교한다. 64 명이 전원 배리어에 도착한 순간에 서버 스레드가 64 개여야 한다(`max_live`). 느린 독자, RST 종료, fd 누수 0 도 본다.

### Part B — 논블로킹 상태기계 + select

- **TODO B1** `conn_flush(c, st)`: 대기 출력 `buf[off, len)` 을 EAGAIN 이 날 때까지 쓴다. 다 쓰면 비우고 `NEXT_READ` 를 돌려준다. EAGAIN 이면 `eagain++` 하고 `NEXT_WRITE`, 오류면 `NEXT_CLOSE` 다.
- **TODO B2** `conn_on_readable(c, st, drain)`: 대기 출력이 남아 있으면 **읽지 않는다**(역압 → `NEXT_WRITE`). 아니면 `CHUNK` = 16 KB 를 읽고 바로 flush 한다. `drain=false` 면 read 를 한 번만 하고, `true` 면 EAGAIN 이나 대기 출력이 생길 때까지 반복한다.
- **TODO B3** `sel_fd_ok(fd)`: `0 <= fd < FD_SETSIZE` 인지 본다. 밖이면 accept 직후 닫고 `rejected++` 한다.
- **TODO B4** `sel_build_sets(head, rs, ws)`: 연결마다 원하는 방향 **한 쪽만** 집합에 넣고 최대 fd 를 돌려준다.
- 단위 테스트는 실제 TCP 쌍 위에서 돈다. 상대가 안 읽으면 정확히 EAGAIN 1 번, 상대가 읽으면 다시 비워짐, `3·CHUNK+5` 바이트를 `drain=false` 로 읽으면 read 1 번, `true` 로 읽으면 4 번이어야 한다. 가드 테스트는 `/dev/null` 로 fd 0..1023 을 채운 뒤 연결한다. 그 연결은 거절되고, fd 를 풀면 다음 연결은 정상이어야 한다.

### Part C — kqueue (레벨 · EV_CLEAR)

- **TODO C1** `kq_want(kq, c, want, clear, st)`: 원하는 쪽 필터는 `EV_ADD|EV_ENABLE`, 반대쪽은 `EV_ADD|EV_DISABLE` 로 한 번의 `kevent` 에 넣는다. `udata = c` 로 두고, clear 모드면 둘 다 `EV_CLEAR` 를 붙인다. **이미 같은 상태면 시스템 콜 0 번**이다.
- **TODO C2** `kq_on_event(c, ev, st, clear)`: `EV_ERROR` 면 닫는다. `EVFILT_WRITE` 면 flush 하고, 다 썼고 clear 모드면 곧바로 drain 읽기를 한다. `EVFILT_READ` 면 `conn_on_readable(c, st, clear)` 를 부른다.
- 단위 테스트(레벨/EV_CLEAR 두 번): 데이터 2 B 가 있을 때 READ 이벤트의 `data == 2` 를 확인한다. 읽지 않고 다시 물으면 레벨 1개, EV_CLEAR 0개다. WRITE 로 바꾸면 읽을 데이터가 있어도 WRITE 만 뜬다. 같은 want 를 다시 주면 `kq_changes` 가 그대로다.
- 서버 스위트를 레벨 모드와 EV_CLEAR 모드로 각각 돌린다. 1000 연결 부하는 `demo_load.c` 로 잰다(§5 Part C 끝).

## 2. Problem (English)

> Implement the same TCP echo server (echo back exactly the bytes received) three ways, all listening on `127.0.0.1` port 0 (read the port back with `getsockname`), each running in its own thread and stopped through a wake pipe:
>
> 1. **Thread-per-connection.** `int write_all(int fd, const unsigned char *p, size_t n, srv_stats_t *st)` writes all `n` bytes, retrying on `EINTR` and continuing after short writes. `void thread_conn_loop(int fd, srv_stats_t *st)` echoes until EOF or error.
> 2. **Non-blocking + `select`.** Each connection keeps a single pending-output buffer. `int conn_flush(conn_t *c, srv_stats_t *st)` writes pending bytes until done (`NEXT_READ`), `EAGAIN` (`NEXT_WRITE`) or error (`NEXT_CLOSE`). `int conn_on_readable(conn_t *c, srv_stats_t *st, bool drain)` must not read while output is pending (backpressure). It reads up to 16 KiB and flushes immediately. If `drain` is set, it repeats until `EAGAIN`. `bool sel_fd_ok(int fd)` guards `FD_SETSIZE`. `int sel_build_sets(const conn_t *head, fd_set *rs, fd_set *ws)` puts each connection in exactly one set.
> 3. **kqueue.** `int kq_want(int kq, conn_t *c, int want, bool clear, srv_stats_t *st)` enables exactly one of `EVFILT_READ`/`EVFILT_WRITE`, optionally with `EV_CLEAR`, and makes no syscall if nothing changes. `int kq_on_event(conn_t *c, const struct kevent *ev, srv_stats_t *st, bool clear)` dispatches an event.
>
> Tests: 64 concurrent clients each send messages of 1, 100, 4096, 65536 and 300000 bytes while reading concurrently, and verify the echo byte-for-byte. Other tests cover a slow reader that forces `EAGAIN`, an RST close, and a select server that must reject a connection whose fd is ≥ 1024. No fd may leak. Every wait has a deadline.
>
> Follow-ups: load test with 1000 concurrent connections and compare throughput, p50/p99 RTT and server RSS. Why does `select` lose when most connections are idle? What changes with `EV_CLEAR` / edge-triggered epoll? Where does each model keep per-connection state?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
```

`/* BUILD: -pthread */` 가 2번째 줄에 있다. 스레드를 쓰므로 `-pthread` 가 필요하다. 처음 실행한 실제 출력 일부다. 약 4초 만에 exit 1 로 끝난다.

```text
== Part A: write_all ==
FAIL starter.c:710  rc == 0 && st_get(&st.bytes_out) == (long)n
== Part A: thread-per-connection ==
PASS server_start(&s, cfg) == 0 && s.port > 0
  client 0: read: Connection reset by peer
FAIL starter.c:628  smoke == 1
SKIP 64-client / slow-reader / RST tests (smoke failed)
…
== Part B: FD_SETSIZE guard ==
PASS ok == 0 && st_get(&s.st.rejected) == 1
FAIL starter.c:810  ok == 1 && st_get(&s.st.rejected) == 1
…
FAIL starter.c:831  kq_want(kq, c, NEXT_READ, clear, &st) == 0 && c->want == NEXT_READ
…
21/49 passed
```

TODO 는 A1·A2·B1–B4·C1·C2 여덟 개다. 스텁은 "틀렸지만 안전한" 값을 돌려준다. 연결을 바로 닫고, 가드는 전부 거절하고, kqueue 등록은 실패를 보고한다. 그래서 클라이언트는 매달리지 않고 곧바로 EOF/RST 를 본다. 스모크 테스트(클라이언트 1개)가 실패하면 그 서버의 64 연결 · 느린 독자 · RST 테스트는 건너뛴다(`SKIP`). 시작 코드의 총 개수(49)가 정답(69)보다 적은 이유다. 가드 테스트의 첫 CHECK 는 시작 코드에서도 통과한다(전부 거절하니까). 둘째 CHECK 가 그 "가짜 통과"를 잡는다.

## 4. 힌트

<details><summary>힌트 1 — write_all 의 두 가지 함정</summary>

`write` 는 요청한 것보다 **적게** 쓰고 돌아올 수 있다(POSIX: 부분 쓰기). 신호에 끊기면 `-1/EINTR` 이다. 루프는 `while (n > 0) { w = write(...); if (w < 0) { if (errno == EINTR) continue; return -1; } p += w; n -= w; }` 모양이다. 짧은 쓰기를 셀 때는 `w < n` 을 **빼기 전에** 비교한다.
</details>

<details><summary>힌트 2 — 왜 대기 출력이 있으면 읽지 않나</summary>

읽은 것을 다 못 썼는데 또 읽으면 출력 버퍼가 끝없이 커진다. 상대가 안 읽는 동안 메모리가 상대 송신량만큼 늘어난다. 읽기를 멈추면 커널 수신 버퍼가 차고, TCP 윈도가 닫히고, 상대의 `write` 가 막힌다. 이것이 **역압**이다. 덕분에 연결당 메모리가 `CHUNK` 하나로 묶인다.
</details>

<details><summary>힌트 3 — conn_on_readable 의 반환값</summary>

`read` 결과 >0 이면 `len = r; off = 0;` 하고 flush 결과를 돌려준다(drain 이면 `NEXT_READ` 일 때만 반복). `0` 은 상대가 닫은 것이니 `NEXT_CLOSE` 다. `-1/EAGAIN` 은 더 읽을 게 없다는 뜻이니 `NEXT_READ` 다. 나머지 오류(ECONNRESET 등)는 `NEXT_CLOSE` 다.
</details>

<details><summary>힌트 4 — kq_want 한 번에 두 필터</summary>

`struct kevent ch[2]` 에 `EV_SET(&ch[0], fd, EVFILT_READ, EV_ADD | cl | (want==READ ? EV_ENABLE : EV_DISABLE), 0, 0, c)` 를, WRITE 도 같은 식으로 넣고 `kevent(kq, ch, 2, NULL, 0, NULL)` 한 번으로 보낸다. `EV_ADD` 는 이미 있으면 수정이다. 맨 앞에서 `if (c->want == want) return 0;` 하는 것을 잊지 않는다.
</details>

<details><summary>힌트 5 — EV_CLEAR 에서 멈추는 경우</summary>

EV_CLEAR 는 이벤트를 한 번 꺼내면 상태가 리셋된다. 쓰기 대기(WRITE) 중에 도착한 데이터에 대해서는 "새" 읽기 이벤트가 안 올 수 있다. 그래서 WRITE 가 끝나 출력이 비면 **이벤트를 기다리지 말고 바로 읽기를 시도**한다(drain). 읽기는 EAGAIN 을 보거나 다시 출력이 막힐 때까지 반복한다.
</details>

## 5. 풀이

### Part A — 연결당 스레드: 상태는 스택에 있다

```c
static void thread_conn_loop(int fd, srv_stats_t *st) {
    unsigned char buf[CHUNK];                 /* 연결 상태 = 이 스택 프레임 */
    for (;;) {
        ssize_t r = read(fd, buf, sizeof buf);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return;
        ...
        if (write_all(fd, buf, (size_t)r, st) != 0) return;
    }
}
```

OSTEP 33.7 이 말하는 대로, 스레드 모델에서는 "다음에 무엇을 할지"가 스택(지역 변수 `buf`, `fd`, 루프 위치)에 저절로 남는다. `write_all` 이 버퍼가 차서 잠들어도 상관없다. 이 스레드가 맡은 연결은 하나뿐이다. 대가는 **연결마다 스레드 하나**라는 것이다.

블로킹 쓰기가 짧게 끝난 적이 있었나? 테스트 통계에서 `short=0` 이었다. 이 맥의 블로킹 TCP `write` 는 64 연결 × 300000 B 에서 한 번도 짧게 돌아오지 않았다(다 쓸 때까지 잤다). 그래도 POSIX 는 짧은 쓰기를 허용한다. 신호가 끼면 그렇게 되고, 소켓 종류에 따라서도 다르다. 그래서 `write_all` 루프는 항상 필요하다.

```text
  [Part A: thread-per-connection] accepted=67 rejected=0 max_live=64 in=24418762 out=24402378 reads=1717 writes=1716 short=0 eagain=0 loops=7 kq_changes=0
```

`max_live=64` 는 클라이언트 64 개가 모두 메시지를 끝내고 배리어에서 기다리는 순간 연결 스레드가 64 개였다는 뜻이다. `in − out = 16384` 는 RST 클라이언트 몫이다. 64 KB 를 보내고 읽지 않은 채 RST 로 끊었으므로 서버의 마지막 쓰기가 실패했다.

하네스에서 눈여겨볼 macOS 사실 두 가지가 있다.

| 사실 | 확인 방법 | 결과 (이 맥) |
|---|---|---|
| accept 한 소켓은 listen 소켓의 `O_NONBLOCK` 을 물려받는다 | 연습 4 (`fcntl(a, F_GETFL) & O_NONBLOCK`) | 물려받음. 리눅스 `accept(2)` 는 물려받지 않는다고 man page 에 적혀 있다 → 스레드 모델은 accept 직후 `set_nonblock(fd, false)` |
| 끊긴 소켓에 쓰면 SIGPIPE | 연습 4 (`SIGPIPE` 무시 후 쓰기) | `write after peer close -> Broken pipe` (EPIPE). 무시하지 않으면 프로세스가 죽는다 → main 첫 줄에서 `sigaction(SIGPIPE, SIG_IGN)` |

정지도 설계해야 한다. 블로킹 `read` 에 잠든 스레드는 `close(fd)` 로 깨우면 안 된다. 다른 스레드가 쓰는 fd 를 닫으면 그 번호가 곧바로 재사용되어 엉뚱한 연결을 건드릴 수 있다. 그래서 정지 쪽은 락을 잡고 `shutdown(fd, SHUT_RDWR)` 만 한다. 연결 스레드는 깨어나서 **락 안에서** `r->fd = -1` 로 바꾼 뒤에 `close` 한다. 이 순서 덕분에 정지 쪽 `shutdown` 과 스레드의 `close` 가 다른 번호를 건드릴 일이 없다.

### Part B — 논블로킹: 상태를 직접 들고 다닌다

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
<defs><marker id="m12b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" fill="currentColor" font-weight="bold">conn_t 상태기계 (select · kqueue 공통, 대기 출력 버퍼 = 1 개)</text>
<rect x="40" y="90" width="170" height="64" rx="8" class="f-ok" stroke="currentColor"/>
<text x="56" y="114" fill="currentColor" font-weight="bold">NEXT_READ</text>
<text x="56" y="132" fill="currentColor" font-size="12">off == len (출력 없음)</text>
<text x="56" y="148" fill="currentColor" font-size="12">관심: 읽기만</text>
<rect x="380" y="90" width="190" height="64" rx="8" class="f-warn" stroke="currentColor"/>
<text x="396" y="114" fill="currentColor" font-weight="bold">NEXT_WRITE</text>
<text x="396" y="132" fill="currentColor" font-size="12">buf[off, len) 남음</text>
<text x="396" y="148" fill="currentColor" font-size="12">관심: 쓰기만 (읽기 멈춤 = 역압)</text>
<rect x="620" y="200" width="110" height="48" rx="8" class="f-mut" stroke="currentColor"/>
<text x="634" y="228" fill="currentColor" font-weight="bold">NEXT_CLOSE</text>
<path d="M210 104 C 280 70, 320 70, 380 104" fill="none" stroke="currentColor" marker-end="url(#m12b)"/>
<text x="218" y="62" fill="currentColor" font-size="12">읽힘 → read(CHUNK) → write 가 EAGAIN</text>
<path d="M380 142 C 320 178, 280 178, 210 142" fill="none" stroke="currentColor" marker-end="url(#m12b)"/>
<text x="222" y="196" fill="currentColor" font-size="12">쓰기 가능 → flush 완료 (off = len = 0)</text>
<text x="222" y="212" fill="currentColor" font-size="12">EV_CLEAR: 여기서 곧바로 drain 읽기</text>
<path d="M40 122 C 0 122, 0 60, 60 82" fill="none" stroke="currentColor" marker-end="url(#m12b)"/>
<text x="10" y="44" fill="currentColor" font-size="12">읽힘 → read → 전부 써짐 (머무름)</text>
<path d="M570 140 C 640 150, 670 170, 675 200" fill="none" stroke="currentColor" marker-end="url(#m12b)"/>
<text x="590" y="130" fill="currentColor" font-size="12">EPIPE · RST</text>
<path d="M125 154 C 160 250, 500 250, 620 226" fill="none" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#m12b)"/>
<text x="230" y="262" fill="currentColor" font-size="12">read 가 0 (EOF) 또는 오류 → close, 목록에서 제거</text>
</svg>
```

핵심은 **대기 출력을 한 칸만 두는 것**이다. 읽은 16 KB 를 다 못 쓰면 남은 바이트를 들고 쓰기 대기 상태로 간다. 그동안은 읽지 않는다. 결과는 세 가지다.

- 연결당 메모리가 `sizeof(conn_t)` ≈ 16 KB 로 고정된다. 실측 1000 연결에서 16.1 KB/연결이었다(§ 부하 결과).
- 상대가 안 읽으면 커널 버퍼 → TCP 윈도 → 상대의 `write` 순서로 막힌다. 역압이 저절로 생긴다.
- 서버가 상대 대신 데이터를 무한히 쌓아 두는 일이 없다(메모리 DoS 방지).

클라이언트도 조심해야 한다. 300000 B 를 **다 보낸 다음에** 읽기 시작하면 교착이 날 수 있다. 서버는 내 수신 버퍼가 차서 echo 를 못 쓰고, 그래서 읽기를 멈춘다. 나는 서버 수신 버퍼가 차서 보내기에서 막힌다. 그래서 하네스의 클라이언트는 `poll(POLLIN|POLLOUT)` 로 보내면서 읽는다. "느린 독자" 테스트는 반대로 일부러 안 읽는다. 서버가 `eagain > 0` 을 보고할 때까지 기다렸다가 읽기 시작한다. 그래서 EAGAIN 경로가 **매번** 실행된다. 시간에 기대지 않고 서버 상태를 문으로 쓴 것이다.

`conn_flush` 와 `conn_on_readable` 의 단위 테스트 결과는 다음과 같다(실측, 매 실행 같음).

| 상황 | 기대 | 왜 결정적인가 |
|---|---|---|
| 상대가 안 읽는 TCP 쌍에 16 KB 씩 계속 flush | 처음 `NEXT_WRITE` 일 때 `eagain == 1`, `off < len` | 버퍼는 유한하다. 첫 EAGAIN 에서 멈춘다 |
| 상대가 읽어 줌 → 다시 flush | `NEXT_READ`, `off == len == 0`, 받은 바이트 = 보낸 바이트 | 바이트 수를 끝까지 비교 |
| `3·16384 + 5` B 도착 후 `drain=false` | read 1 번, `bytes_in == 16384` | `MSG_PEEK` 로 전부 도착을 확인한 뒤 호출 |
| 이어서 `drain=true` | read 4 번(16384, 16384, 5, 그다음 EAGAIN) | EAGAIN 까지 |
| 대기 출력 10 B 가 있는 채로 읽기 알림 | `NEXT_WRITE`, read 0 번 | 역압 규칙 |

**select 의 비용.** 루프마다 ① `fd_set` 두 개를 0 에서 다시 만들고(연결 수만큼), ② 커널에 128 B × 2 를 복사하고, 커널은 fd 0..maxfd 를 전부 검사한다. ③ 돌아온 비트맵에서 준비된 fd 를 찾으려고 또 연결 목록을 전부 돈다. 연결이 1000 개인데 바쁜 것이 10 개뿐이어도 이 일은 매번 1000 개 분량이다. 부하 결과의 "10 활성" 표가 이 비용을 보여 준다.

**FD_SETSIZE.** `fd_set` 은 1024 비트짜리 고정 배열이다(`sizeof(fd_set)` = 128 B, 실측). `man 2 select` 에는 fd ≥ `FD_SETSIZE` 일 때 매크로 동작이 정의되지 않았다고 적혀 있고, COMPATIBILITY 절에는 `_DARWIN_UNLIMITED_SELECT` 없이 `nfds > FD_SETSIZE` 면 `EINVAL` 이라고 적혀 있다. 이 레슨의 파일은 `_POSIX_C_SOURCE` 만 켜서 빌드하므로 제한 있는 select 가 링크된다. `demo_fdset.c` 로 가드 없이 넣으면 무슨 일이 나는지 직접 봤다.

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_fdset.c -o fd_strict && ./fd_strict
cc -std=c11 -Wall -Wextra -Werror -O2 -DUNLIMITED demo_fdset.c -o fd_unl && ./fd_unl
```

<!-- code: demo_fdset.c -->

```text
build: strict POSIX, FD_SETSIZE=1024, sizeof(fd_set)=128 bytes
select(nfds=1025, empty sets, timeout 0) -> -1 (Invalid argument)
stack fd_set: FD_SET(1050) -> FD_ISSET=0, guard bytes after fd_set changed=0
heap  fd_set: FD_SET(1050) -> FD_ISSET=1, guard bytes after fd_set changed=1
build: _DARWIN_UNLIMITED_SELECT, FD_SETSIZE=1024, sizeof(fd_set)=128 bytes
select(nfds=1025, empty sets, timeout 0) -> 0 (ok)
stack fd_set: FD_SET(1050) -> FD_ISSET=1, guard bytes after fd_set changed=1
heap  fd_set: FD_SET(1050) -> FD_ISSET=1, guard bytes after fd_set changed=1
```

출력을 읽는 법은 이렇다. SDK 26.4 의 `FD_SET` 은 인라인 함수 `__darwin_fd_set` 이고, 쓰기 전에 libc 의 `__darwin_check_fd_set_overflow()` 로 검사한다(`sys/_types/_fd_def.h` 에서 확인). strict 빌드에서는 **스택**의 `fd_set` 에 1050 을 넣으려 하면 조용히 무시된다. 비트가 안 켜지니 그 연결은 select 에 영원히 안 잡히고, 클라이언트는 그냥 매달린다. 힙 객체나 UNLIMITED 빌드에서는 실제로 `fd_set` 뒤의 바이트를 덮어쓴다(guard 1 바이트 변경 = 메모리 손상). 힙에서만 허용되는 이유는 할당 크기(`malloc_size`) 안이면 통과시키기 때문으로 **추정**한다. 출력만 보고 한 추론이다. 어느 쪽이든 가드가 답이다. 서버의 `sel_fd_ok` 는 accept 직후 fd ≥ 1024 를 닫는다. 테스트 실측: `filled 1018 fds (last 1023), rejected=1`. 클라이언트는 `Connection reset by peer` 를 받는다.

리눅스 glibc 도 `FD_SETSIZE` 가 1024 다. `_FORTIFY_SOURCE` 빌드에서는 범위 밖 `FD_SET` 이 런타임 오류로 끝난다고 알려져 있다(이 맥에서 확인하지 않음). 큰 fd 가 필요하면 `poll`(연습 1, 같은 O(n) 이지만 상한 없음)이나 kqueue/epoll 로 간다.

### Part C — kqueue: 바뀔 때만 말하고, 준비된 것만 듣는다

```c
static int kq_want(int kq, conn_t *c, int want, bool clear, srv_stats_t *st) {
    if (c->want == want) return 0;                       /* 바뀐 게 없으면 시스템 콜 0 */
    unsigned short cl = clear ? EV_CLEAR : 0;
    struct kevent ch[2];
    EV_SET(&ch[0], c->fd, EVFILT_READ,  EV_ADD | cl | (want == NEXT_READ  ? EV_ENABLE : EV_DISABLE), 0, 0, c);
    EV_SET(&ch[1], c->fd, EVFILT_WRITE, EV_ADD | cl | (want == NEXT_WRITE ? EV_ENABLE : EV_DISABLE), 0, 0, c);
    ...
}
```

- select 는 관심 목록 전체를 매번 커널에 다시 알린다. kqueue 는 **바뀔 때만** 알린다. 관심이 커널의 knote 에 남아 있기 때문이다.
- 돌려받는 것도 준비된 이벤트뿐이다. `udata` 에 `conn_t*` 를 넣어 두면 fd → 연결 조회도 필요 없다.
- `EVFILT_WRITE` 는 **출력이 남았을 때만** 켠다. 소켓은 거의 항상 쓰기 가능하므로 늘 켜 두면 루프가 쓸 것도 없이 계속 깨어난다(레벨 모드에서 바쁜 대기).

레벨 vs EV_CLEAR 는 man 2 kqueue 가 `EV_CLEAR` 를 이렇게 정의한다. "After the event is retrieved by the user, its state is reset." 단위 테스트에서 2 B 가 도착한 상태로 읽지 않고 두 번 물어본 실제 출력이다.

```text
  level: second kevent without reading -> 1 event(s)
  EV_CLEAR: second kevent without reading -> 0 event(s)
```

레벨 모드는 "아직 읽을 게 있다"를 계속 알려 준다. 그래서 처리기가 한 번에 16 KB 만 읽어도 된다. 다른 연결과 번갈아 처리하니 공정하기도 하다. EV_CLEAR(리눅스 EPOLLET 와 같은 "에지" 의미)는 새 데이터가 와야 다시 알린다. 그래서 처리기가 **EAGAIN 까지 비워야** 한다. 비우지 않으면 남은 데이터가 영원히 잠든다. 이 버그의 epoll 판 재현은 OS-M13 이 한다. 여기서 EV_CLEAR 모드가 맞게 도는 이유는 두 가지다. `kq_on_event` 가 READ 에서 `drain=true` 로 읽고, WRITE 를 다 비운 직후에도 이벤트를 기다리지 않고 바로 drain 읽기를 하기 때문이다.

같은 64 연결 테스트를 돈 서버 통계다(실측).

```text
  [Part B: select] accepted=67 rejected=0 max_live=64 in=24418762 out=24402378 reads=1717 writes=2250 short=534 eagain=1039 loops=457 kq_changes=0
  [Part C: kqueue (level)] accepted=67 rejected=0 max_live=64 in=24418762 out=24402378 reads=1717 writes=2442 short=726 eagain=911 loops=237 kq_changes=1883
  [Part C: kqueue (EV_CLEAR)] accepted=67 rejected=0 max_live=64 in=24435146 out=24418762 reads=1718 writes=2028 short=311 eagain=1373 loops=266 kq_changes=331
```

| 지표 | 읽는 법 |
|---|---|
| `short` · `eagain` > 0 | 서버 `SO_SNDBUF` 를 16 KB 로 줄여서 부분 쓰기와 EAGAIN 이 수백 번 났다. 그래도 바이트가 한 개도 틀리지 않았다 = 대기 출력 처리가 맞다 |
| `kq_changes` 1883 vs 331 | 레벨 모드는 쓰기를 끝내면 READ 로 돌아갔다가(등록 1번), 다음 read 에서 또 막히면 WRITE 로 간다(등록 1번). EAGAIN 1 번에 등록 2 번꼴이다(911 × 2 + 67 ≈ 1889). EV_CLEAR 모드는 쓰기를 끝낸 자리에서 바로 읽고, 또 막히면 WRITE 에 그대로 머문다. 등록이 거의 필요 없다 |
| `loops` | select 457 번, kqueue 237–266 번 깨어났다. kqueue 는 한 번에 최대 64 개 이벤트를 받는다 |

kqueue 에서 꼭 지켜야 할 것이 하나 더 있다. **한 배치 안에서 닫은 연결을 바로 free 하지 않는다.** 같은 `kevent` 결과 배열 안에 그 연결의 이벤트가 하나 더 있을 수 있기 때문이다. `udata` 로 꺼낸 포인터가 해제된 메모리를 가리키게 된다. 그래서 `conn_close` 는 `fd = -1` 로 표시하고 zombies 목록에 넣는다. 배치가 끝난 뒤에 free 한다(OS-A11 과 같은 장치). `close(fd)` 하면 knote 는 자동으로 지워진다. man kqueue 의 `EV_DELETE` 설명: "Events which are attached to file descriptors are automatically deleted on the last close of the descriptor."

| kqueue (macOS · BSD) | epoll (리눅스, OS-M13) |
|---|---|
| `kqueue()` | `epoll_create1(0)` |
| `EV_SET(.., EVFILT_READ, EV_ADD, ..)` + `kevent` | `epoll_ctl(ADD, EPOLLIN)` |
| `EV_ENABLE` / `EV_DISABLE` | `epoll_ctl(MOD, events)` |
| `EV_CLEAR` | `EPOLLET` |
| `EV_ONESHOT` / `EV_DISPATCH` | `EPOLLONESHOT` |
| 변경 여러 개 + 대기를 시스템 콜 1번으로 | 변경 1개당 `epoll_ctl` 1번 |
| `EVFILT_USER` · `EVFILT_TIMER` · `EVFILT_SIGNAL` · `EVFILT_PROC` | `eventfd` · `timerfd` · `signalfd` · `pidfd` (OS-M18) |

#### 부하 결과 (demo_load.c)

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_load.c -o demo_load
./demo_load                    # 1000 연결 × 64 B 왕복 100 번, 네 모델
./demo_load 1000 2000 all 10   # 1000 연결 중 10 개만 활성
./demo_load 1100 10 select     # FD_SETSIZE 가드
```

<!-- code: demo_load.c -->

`solution.c` 를 `#define ECHO_NO_MAIN` 으로 그대로 include 한다. 서버는 **fork 한 자식**에서 돈다. 그래야 RSS · 스레드 수 · 문맥 교환 · CPU 가 서버 것만 잡힌다. 부하 생성기는 kqueue 하나로 N 개 소켓을 돌린다. 각 연결은 64 B 를 보내고 64 B 가 돌아오면 RTT 를 기록하고 다시 보낸다(닫힌 고리). RSS 는 N 개 연결을 **연 채로** `task_info(MACH_TASK_BASIC_INFO).resident_size` 로 잰다. `getrusage` 의 `ru_maxrss` 는 macOS 에서 **바이트** 단위다(man getrusage: "in bytes"). 리눅스는 KB 다. 데모만 `_DARWIN_C_SOURCE` 를 켠다. strict POSIX 에서는 `ru_nvcsw` 등이 안 보이기 때문이다. 이 때문에 데모의 select 는 제한 없는 변형이 되지만, `sel_fd_ok` 가드가 그대로 막는다.

환경: Apple M6 (12 코어), Apple clang 21.0.0, `kern.maxfilesperproc` = 122880, `kern.maxfiles` = 245760, `kern.ipc.somaxconn` = 128, 셸의 `RLIMIT_NOFILE` = 1048576(데모는 최소 8192 로 올림), 기본 pthread 스택 524288 B. **다른 세션이 돌던 공유 맥**이다(`uptime` load average 16.7–16.8). 절댓값보다 모델 사이 비율을 본다.

```text
20:07  up 9 days, 20:28, 12 users, load averages: 16.73 15.21 12.36
N=1000 connections (1000 active) x M=100 round trips of 64 B, RLIMIT_NOFILE cur=1048576, CHUNK=16384 B/conn buffer
model              N    up   conn/s    msgs/s  p50(us)  p99(us) RSS MB idle→N   KB/c   thr     vcsw   ivcsw  wakeups  cpu s   rej  eof
thread          1000  1000    46922    110843   8891.0  13610.0    1.2→  32.5   32.1  1002   101002   34513      144   1.43     0    0
select          1000  1000    38225    114671   8795.0  14035.0    1.2→  16.9   16.1     2       91    5296     2091   0.86     0    0
kqueue-level    1000  1000    38821     99376   9910.0  19480.0    1.2→  16.9   16.1     2        2   13706     8600   0.78     0    0
kqueue-clear    1000  1000    37190    105490   9284.0  17026.0    1.2→  16.9   16.1     2        2   11197     7451   0.80     0    0
N=1000 connections (10 active) x M=2000 round trips of 64 B, RLIMIT_NOFILE cur=1048576, CHUNK=16384 B/conn buffer
model              N    up   conn/s    msgs/s  p50(us)  p99(us) RSS MB idle→N   KB/c   thr     vcsw   ivcsw  wakeups  cpu s   rej  eof
thread          1000  1000    40912     96525     85.0    262.0    1.2→  32.6   32.1  1002    20969   10757      166   0.31     0    0
select          1000  1000    35894     40270    188.0    626.0    1.2→  16.9   16.1     2      116    1959     4142   0.50     0    0
kqueue-level    1000  1000    39040     80404    110.0    313.0    1.2→  16.9   16.1     2        2    8397     9955   0.21     0    0
kqueue-clear    1000  1000    36007     90362     97.0    211.0    1.2→  16.9   16.1     2        2    3869    11589   0.20     0    0
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">1000 연결 중 10 개만 활성 (실측, 위 표 두 번째 블록)</text>
<text x="10" y="44" fill="currentColor">msgs/s (천)</text>
<text x="390" y="44" fill="currentColor">서버 CPU (초, user+sys)</text>
<text x="10" y="74" fill="currentColor">thread</text>
<rect x="100" y="62" width="241" height="16" class="f-mut" stroke="currentColor"/><text x="346" y="75" fill="currentColor">96.5</text>
<text x="10" y="104" fill="currentColor">select</text>
<rect x="100" y="92" width="101" height="16" class="f-warn" stroke="currentColor"/><text x="206" y="105" fill="currentColor">40.3</text>
<text x="10" y="134" fill="currentColor">kq-level</text>
<rect x="100" y="122" width="201" height="16" class="f-ok" stroke="currentColor"/><text x="306" y="135" fill="currentColor">80.4</text>
<text x="10" y="164" fill="currentColor">kq-clear</text>
<rect x="100" y="152" width="226" height="16" class="f-ok" stroke="currentColor"/><text x="331" y="165" fill="currentColor">90.4</text>
<rect x="390" y="62" width="155" height="16" class="f-mut" stroke="currentColor"/><text x="550" y="75" fill="currentColor">0.31</text>
<rect x="390" y="92" width="250" height="16" class="f-warn" stroke="currentColor"/><text x="645" y="105" fill="currentColor">0.50</text>
<rect x="390" y="122" width="105" height="16" class="f-ok" stroke="currentColor"/><text x="500" y="135" fill="currentColor">0.21</text>
<rect x="390" y="152" width="100" height="16" class="f-ok" stroke="currentColor"/><text x="495" y="165" fill="currentColor">0.20</text>
<line x1="100" y1="56" x2="100" y2="174" stroke="currentColor"/>
<line x1="390" y1="56" x2="390" y2="174" stroke="currentColor"/>
<text x="10" y="200" fill="currentColor">select: 바쁜 연결은 10 개뿐인데 매 루프 1000 개 fd 를 커널과 앱이 둘 다 훑는다 → 처리량 절반, CPU 2.5 배.</text>
<text x="10" y="218" fill="currentColor">thread: 깨어날 스레드가 10 개뿐이라 빠르다. 대가는 메모리(32 KB/연결)와 스레드 1002 개.</text>
<text x="10" y="236" fill="currentColor">load average 16.8 인 공유 맥 1회 측정. 같은 조건 이전 실행: select 33–38k, kqueue 56–74k msgs/s.</text>
</svg>
```

표에서 읽을 것은 다섯 가지다.

1. **전부 바쁠 때(1000/1000)는 처리량이 거의 같다.** 같은 설정을 다섯 번 돌렸을 때 모델별 msgs/s 범위는 thread 90k–112k, select 88k–126k, kqueue-level 99k–119k, kqueue-clear 84k–121k 였다. 순위가 매번 바뀌었다. 64 B 왕복에서는 병목이 부하 생성기(단일 스레드)와 루프백 TCP 스택이지, 서버 모델이 아니다. "이벤트 루프가 항상 빠르다"는 틀린 일반화다.
2. **10/1000 활성에서 select 가 무너진다.** 40k vs 80–90k msgs/s, CPU 0.50 vs 0.20 s 다. 일 = 1000 개 fd × 루프 횟수다. 이것이 C10K 논의의 핵심이고, kqueue/epoll 이 만들어진 이유다.
3. **메모리**: 연결당 32.1 KB(스레드) vs 16.1 KB(이벤트)다. 이벤트 모델의 16 KB 는 `conn_t` 안의 16 KB 버퍼다. 64 B 만 쓰는데도 RSS 가 16 KB 인 것은 이 맥의 페이지가 16 KB 이기 때문이다(`hw.pagesize` = 16384, 첫 쓰기에 한 페이지가 통째로 잡힌다). 스레드 모델의 32 KB 는 스택에 있는 16 KB `buf` 와 스택 윗부분 페이지로 보인다. 기본 스택 512 KB 는 가상 예약일 뿐이다. 커널 쪽 스레드 구조체와 소켓 버퍼는 이 RSS 에 안 들어간다. 연결당 버퍼를 4 KB 로 줄이면 이벤트 모델은 더 줄지만 스레드 모델은 스택 페이지 때문에 덜 준다(연습 아이디어, 측정 안 함).
4. **문맥 교환**: 스레드 모델의 자발적 문맥 교환 101002 ≈ 메시지 100000 + α 다. 메시지마다 연결 스레드가 `read` 에서 잠들었다 깬다. kqueue 는 `vcsw` 가 2 인데, 같은 기간 `kevent` 에서 8600–11589 번 깨어났다(`wakeups` 열). macOS 의 `ru_nvcsw` 는 `kevent` 대기를 거의 세지 않는 것으로 보인다. 그래서 이 열로 스레드 모델과 kqueue 를 직접 비교하면 안 된다. 원인(XNU 의 continuation 기반 대기 등)은 확인하지 않았다. 비교는 CPU 시간으로 한다(1.43 s vs 0.78 s).
5. **연결 수립**: listen backlog 는 `listen(fd, 1024)` 을 해도 128 로 잘린다(man 2 listen BUGS: "The backlog is currently limited (silently) to 128."). 한꺼번에 1000 개를 connect 하면 큐가 넘치고, 이 맥에서는 일부가 **RST(`Connection reset by peer`)** 로 거절됐다. 재시도가 0–276 회였고 실행마다 달랐다. 그래서 데모는 진짜 클라이언트처럼 재시도하고 그 횟수를 찍는다. 재시도 없이 잰 첫 버전에서는 thread 모델 1000 개 중 736–887 개만 붙었다.

FD_SETSIZE 가드를 부하로 확인한 결과다. `./demo_load 1100 10 select` 실측(load average 10.4):

```text
N=1100 connections (1100 active) x M=10 round trips of 64 B, RLIMIT_NOFILE cur=1048576, CHUNK=16384 B/conn buffer
model              N    up   conn/s    msgs/s  p50(us)  p99(us) RSS MB idle→N   KB/c   thr     vcsw   ivcsw  wakeups  cpu s   rej  eof
select          1100  1100    44639    161701   6204.0   9095.0    1.2→  17.2   14.9     2       90     190      524   0.09    84   84
```

84 개 연결이 거절됐다(`rej 84`). 자식 서버에서 fd 1024..1107 에 해당하는 연결들이다. 클라이언트 쪽에서도 정확히 84 개가 EOF 를 봤다. 연결 수립(`up`)은 TCP 수준에서 성공한 뒤 서버가 accept 하고 바로 닫았기 때문에 1100 이다. `KB/c` 14.9 는 거절된 84 개까지 나눈 값이라 조금 작게 나왔다. 앞선 실행에서는 같은 명령에 backlog 넘침으로 인한 RST 재시도 276 회가 함께 찍혔다. 같은 1100 연결을 kqueue 로 돌리면 `rej 0` 이다(실측).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o so_san && ./so_san
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o so_tsan && ./so_tsan
```

실측: 세 빌드 모두 `69/69 passed`, exit 0 이었다. ASan/UBSan 보고와 TSan 경고는 0개다. `-O2` 빌드는 0.4 초 안에 끝난다. 다섯 번 이상 반복했고, 레슨 디렉터리와 `/` 에서 실행해도 같다. 서버 스레드와 클라이언트 스레드가 공유하는 것은 통계(`_Atomic long`)뿐이다. 스레드 모델의 `recs` 와 `rec->fd` 는 뮤텍스가 지킨다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 / 규칙 |
|---|---|---|
| `write(fd, buf, n);` 한 번으로 끝 | 짧은 쓰기 · EINTR 에서 데이터가 사라진다. 블로킹에서도 POSIX 상 가능하다 | `write_all` 루프 (A1) |
| 논블로킹에서 EAGAIN 이면 나머지를 버림 | 300000 B 테스트가 바이트 불일치로 실패한다 | 남은 `buf[off, len)` 을 들고 `NEXT_WRITE` |
| 대기 출력이 있는데 또 읽어서 버퍼에 이어 붙임 | 출력 버퍼가 상대 송신량만큼 자란다 (메모리 DoS) | 출력이 남았으면 읽지 않는다 = 역압 |
| `EVFILT_WRITE` 를 항상 켜 둠 | 소켓은 늘 쓰기 가능 → 레벨 모드에서 할 일 없이 계속 깨어남 | 출력이 남았을 때만 enable |
| EV_CLEAR(또는 EPOLLET)에서 read 한 번 | 남은 데이터에 대한 새 알림이 없다 → 연결이 멈춤 | EAGAIN 까지 drain. 쓰기를 끝낸 직후에도 drain (C2) |
| select 에 fd ≥ 1024 를 `FD_SET` | 이 맥 실측: 스택이면 조용히 무시(연결이 영원히 대기), 힙이면 이웃 메모리 덮어씀 | accept 직후 `sel_fd_ok` 로 거절, 또는 poll/kqueue |
| select 가 끝난 뒤 `fd_set` 을 재사용 | select 는 집합을 **결과로 덮어쓴다** | 루프마다 `FD_ZERO` 후 다시 만든다 |
| 배치 안에서 닫은 `conn_t` 를 바로 free | 같은 배치의 다음 이벤트가 `udata` 로 해제된 메모리를 만짐 | zombies 목록, 배치 끝에서 free |
| 다른 스레드가 블록 중인 fd 를 `close` 로 깨움 | fd 번호가 재사용되어 엉뚱한 연결을 건드림 | `shutdown(SHUT_RDWR)` 으로 깨우고, 닫기는 그 fd 를 쓰는 스레드가 |
| SIGPIPE 를 무시하지 않음 | 끊긴 소켓에 쓰는 순간 서버 프로세스 전체가 죽는다 | `sigaction(SIGPIPE, SIG_IGN)`. 소켓 단위로는 `SO_NOSIGPIPE`(macOS), 호출 단위로는 `MSG_NOSIGNAL`(리눅스. 이 맥 SDK `sys/socket.h` 에도 정의돼 있음) |
| macOS 에서 accept 한 fd 가 블로킹이라고 가정 | listen fd 가 논블로킹이면 물려받는다(실측). 스레드 모델의 `read` 가 EAGAIN 으로 바로 끝남 | 모델에 맞게 명시적으로 `set_nonblock(fd, on)` |
| 클라이언트가 다 보낸 뒤에야 읽음 | 큰 메시지에서 양쪽이 서로 쓰기에서 막혀 교착 | `poll(POLLIN|POLLOUT)` 로 보내면서 읽기 |
| 테스트에 고정 포트 사용 | 병렬 실행 · TIME_WAIT 에서 `EADDRINUSE` | 포트 0 + `getsockname` |

## 8. 면접 Q&A

<details><summary>Q1. 연결당 스레드 서버와 이벤트 루프 서버를 비교해 보세요. 언제 무엇을 고르나요?</summary>

스레드 모델은 코드가 순차적이다. 연결 상태가 스택에 있고, 블로킹 호출(디스크, DNS, 라이브러리)이 그 스레드만 멈춘다. 대가는 연결마다 스택(실측 RSS 32 KB/연결, 가상 512 KB), 커널 스레드, 메시지마다 문맥 교환(실측 vcsw ≈ 메시지 수), 공유 상태의 락이다. 이벤트 루프는 스레드 하나로 수천 연결을 다루고 루프 안에 락이 없다. 연결당 메모리는 직접 정한 버퍼 크기다(실측 16 KB). 대신 상태기계를 직접 써야 하고, 처리기가 블록하면 서버 전체가 멈춘다. 그래서 디스크 I/O 는 워커 풀에 넘긴다(OS-A11). 연결이 수십 개고 일이 CPU/블로킹 위주면 스레드나 스레드 풀을 고른다. 연결이 많고 대부분 놀고 있으면 이벤트 루프를 고른다. 코어를 다 쓰려면 루프를 코어 수만큼 둔다(SO_REUSEPORT 등 → NET-A07). 실측으로는 1000 개가 전부 바쁜 64 B 에코에서 처리량 차이가 없었다. 10/1000 활성에서는 thread 96k, kqueue 80–90k, select 40k msgs/s 였다.
</details>

<details><summary>Q2. select 의 한계는 정확히 무엇인가요? poll 은 무엇을 고쳤고 무엇을 못 고쳤나요?</summary>

① `fd_set` 은 `FD_SETSIZE` = 1024 비트 고정 배열이라 fd 값 1024 이상을 넣을 수 없다. 연결 **수**가 아니라 fd **번호**가 기준이다. ② 집합을 결과로 덮어쓰므로 매 호출 다시 만들어야 한다. ③ 커널이 0..maxfd 전부를 검사하고, 앱도 결과를 찾으려 다시 전부 돈다. 매번 O(maxfd) 다. poll 은 `struct pollfd` 배열이라 ①을 고쳤다(연습 1: fd 1024/1025 에서 300000 B 에코 성공). `events` 와 `revents` 를 나눠 ②도 고쳤다. 하지만 ③은 그대로다. 관심 목록 전체를 매번 커널에 넘긴다. kqueue/epoll 은 관심을 커널에 등록해 두고 준비된 것만 돌려줘서 ③을 고쳤다. 실측: 10/1000 활성에서 select 의 처리량은 kqueue 의 약 절반, CPU 는 2.5 배였다.
</details>

<details><summary>Q3. 레벨 트리거와 에지 트리거(EV_CLEAR, EPOLLET)의 차이와 함정은?</summary>

레벨은 "조건이 참인 동안" 매번 알린다. 읽을 데이터가 남아 있으면 다음 대기에서 또 뜬다. 에지는 "상태가 바뀔 때" 한 번 알린다. 새 데이터가 와야 다시 뜬다. 실측: 2 B 를 읽지 않고 두 번 물으면 레벨 1개, EV_CLEAR 0개였다. 에지의 함정은 처리기가 일부만 읽으면 나머지가 영원히 잠든다는 것이다. 그래서 EAGAIN 까지 drain 해야 한다. 쓰기 대기 동안 도착한 데이터도 마찬가지라서, 출력을 다 비운 직후 이벤트를 기다리지 말고 바로 읽어야 한다. 에지의 장점은 불필요한 재알림이 없다는 것이다. 이 레슨에서는 관심 변경 등록이 1883 → 331 번으로 줄었다. 단점은 drain 때문에 한 연결이 루프를 오래 붙잡아 공정성이 나빠질 수 있다는 것이다(읽기 상한을 두기도 한다). epoll 판 재현과 EPOLLONESHOT 은 OS-M13 에서 다룬다.
</details>

<details><summary>Q4. 논블로킹 소켓에서 write 가 일부만 쓰였거나 EAGAIN 이면 어떻게 하나요? 꼬리: 메모리는 어떻게 묶나요?</summary>

남은 바이트를 연결의 출력 버퍼에 들고, 쓰기 가능 알림(`EVFILT_WRITE` / `EPOLLOUT`)을 켠다. 알림이 오면 이어서 쓰고, 다 쓰면 알림을 끈다. 메모리를 묶는 방법은 **역압**이다. 출력이 남아 있는 동안 그 연결에서 읽지 않는다. 그러면 커널 수신 버퍼가 차고, TCP 윈도가 0 이 되고, 송신자가 막힌다. 연결당 메모리는 버퍼 하나로 고정된다. 여러 메시지를 큐에 쌓는 설계라면 하이/로 워터마크를 두고 하이를 넘으면 읽기를 끈다. 테스트는 서버 `SO_SNDBUF` 를 16 KB 로 줄였다. 64 연결 동안 EAGAIN 이 911–1373 번, 짧은 쓰기가 311–726 번 났지만 바이트는 하나도 틀리지 않았다.
</details>

<details><summary>Q5. 이벤트 루프에서 하면 안 되는 일은? 하면 어떻게 되나요?</summary>

블록하는 모든 일이다. 디스크 `read`(페이지 캐시 미스), `fsync`, DNS `getaddrinfo`, 락 대기, 큰 계산이 여기에 든다. 루프가 하나뿐이라 그 동안 **모든** 연결이 멈춘다(OSTEP 33.5). 페이지 폴트도 루프를 멈춘다(33.8). 해법은 세 가지다. 블로킹 일은 워커 스레드 풀에 넘기고, 완료는 파이프 · `EVFILT_USER` · eventfd 로 루프를 깨워 알린다(연습 3, OS-A11). 비동기 API(io_uring → LLAT-A03)를 쓸 수도 있다. 긴 계산은 쪼개서 루프에 양보한다. 펌웨어에서는 ISR 에서 오래 걸리는 일을 하지 말라는 규칙과 같다(C-M14). 이벤트 처리기는 ISR 처럼 짧게, 나머지는 태스크로 넘긴다.
</details>

<details><summary>Q6. 동시 연결 수의 상한은 어디서 정해지나요? (이 맥 기준)</summary>

fd 는 프로세스당 `RLIMIT_NOFILE`(소프트/하드)이고 시스템 상한은 `kern.maxfilesperproc` = 122880, `kern.maxfiles` = 245760 이다(실측 sysctl). select 는 그와 무관하게 fd 번호 1024 미만이다. 스레드 모델은 `kern.num_taskthreads` = 8192 와 메모리가 한계다. 클라이언트 쪽은 임시 포트 범위가 한계다. `net.inet.ip.portrange.first..last` = 49152..65535 라서 목적지 하나당 약 16k 연결이다. accept 쪽은 listen backlog 가 128 로 잘린다. 실측: 1000 개 동시 connect 에서 RST 거절과 재시도가 0–276 회였다. 리눅스는 `ulimit -n`, `fs.file-max`, `net.core.somaxconn`, `net.ipv4.ip_local_port_range` 가 대응한다(값은 배포판마다 다름).
</details>

<details><summary>Q7. 임베디드에서는 이 선택이 어떻게 나타나나요?</summary>

임베디드 리눅스 데몬(게이트웨이, 모뎀 관리자, 센서 허브)은 대부분 단일 스레드 이벤트 루프 하나다. 소켓, UART fd, timerfd, signalfd 를 한 루프에서 다룬다(libevent, glib main loop, systemd sd-event 같은 라이브러리도 이 구조다). RAM 이 작아서 연결마다 스택을 둘 여유가 없다. MCU + lwIP 에서는 같은 선택이 API 로 나타난다. **socket/netconn API** 는 블로킹이라 연결마다 RTOS 태스크와 스택(수 KB)이 필요하다. **raw API** 는 수신 · 송신 완료 콜백으로 도는 이벤트 모델이다. lwIP 에도 `lwip_select` 와 `FD_SETSIZE` 류 상한(`MEMP_NUM_NETCONN`)이 있다. 이 lwIP 내용은 기억에 기댄 것이라 버전마다 확인이 필요하다. 어느 쪽이든 "남은 출력을 들고 TX 가 빌 때 이어서 보낸다"는 패턴은 UART 링버퍼 TX 인터럽트(BM-M04)와 같다. TXE 인터럽트를 보낼 게 있을 때만 켜는 것은 `EVFILT_WRITE` 를 출력이 있을 때만 켜는 것과 같다.
</details>

<details><summary>Q8. 테스트를 결정적으로 만든 방법은? (꼬리: "EAGAIN 경로를 정말 탔는지 어떻게 알죠?")</summary>

① 고정 포트를 쓰지 않는다(포트 0). ② 모든 대기에 마감 시각을 둔다(poll 10 ms 조각 + deadline, 배리어도 timed). 그래서 시작 코드도 4초 안에 FAIL 로 끝난다. ③ 64 연결 동시성은 **배리어**로 만든다. 각 클라이언트가 echo 를 끝낸 뒤 배리어에서 기다리므로, 그 순간 64 연결이 모두 열려 있다 → `max_live >= 64` 가 매번 참이다. ④ EAGAIN 경로는 시간이 아니라 **서버 상태로 문을 연다**. 느린 독자는 서버의 `eagain` 카운터가 0 보다 커질 때까지 읽지 않는다. 서버 송신 버퍼 16 KB + 클라이언트 수신 버퍼 16 KB 를 300000 B 가 넘으므로 올바른 서버는 반드시 EAGAIN 을 만난다. ⑤ 통계는 `write` 가 돌아온 **뒤에** 오르므로, 클라이언트가 다 받았어도 카운터가 아직 덜 올랐을 수 있다. 바이트 합계는 짧게 기다렸다 비교한다(`wait_stat`). ⑥ fd 누수는 `fcntl(F_GETFD)` 로 전 범위를 세어 시작 전과 후를 비교한다.
</details>

## 9. 추가 연습

### 연습 1 — poll() 로 FD_SETSIZE 넘기

`struct pollfd` 배열로 같은 에코 루프를 만든다. 대기 출력이 있으면 `POLLOUT`, 없으면 `POLLIN` 을 건다. fd 0..1023 을 `/dev/null` 로 채운 뒤 연결해서 클라이언트 fd ≥ 1024 에서 1 B, 65536 B, 300000 B 에코가 되는지 확인한다.

<details><summary>답</summary>

실측 출력은 `client fd=1024, server max accepted fd=1025` 였고, 세 크기 모두 바이트 일치했다. poll 에는 비트맵 상한이 없다. `nfds` 는 배열 길이고 `RLIMIT_NOFILE` 만 따른다. 하지만 매 호출 배열 전체를 커널에 복사하고 전부 검사하는 O(n) 비용은 select 와 같다. 그래서 "fd 번호 상한만 없앤 select"다. 슬롯 배열과 pollfd 배열을 나란히 두고 `idx[]` 로 되짚는 것은 select 의 "준비된 fd 찾기 순회"와 같은 일이다.
</details>

### 연습 2 — 바이트 스트림 위의 줄 프레이밍

TCP 는 메시지 경계를 보존하지 않는다. `"AT\r\nAT+GMR\r\nOK\n"` 을 **모든** 2분할 · 3분할 위치로 쪼개 넣어도 같은 세 줄(`AT`, `AT+GMR`, `OK`)이 나오는 `lf_feed` 를 만든다. 32 자를 넘는 줄은 다음 `\n` 까지 버리고 `nerr++` 한다. socketpair 로 한 바이트씩 보낸 뒤 5 바이트씩 읽어도 같아야 한다.

<details><summary>답</summary>

상태는 `line[]`, `len`, `overflow` 셋이다. `\n` 을 만나면 줄 하나를 내보낸다(끝의 `\r` 제거). 넘친 줄이었다면 `nerr++` 만 하고 상태를 리셋한다. 넘치기 시작하면 다음 `\n` 까지 버린다. 이 "쪼개진 입력에 불변" 성질이 프레이머의 본질이다. 모뎀 AT 명령 파서(RF-M08), UART 콘솔, NET-B10 의 길이 접두 프레이밍이 모두 같은 구조다. 길이 상한이 없으면 악의적인 피어 하나가 메모리를 다 먹는다.
</details>

### 연습 3 — 파이프 없이 루프 깨우기: EVFILT_USER, 그리고 EVFILT_TIMER

kqueue 에 `EVFILT_USER`(ident 1, `EV_CLEAR`)를 등록한다. 다른 스레드가 50 ms 뒤 `NOTE_TRIGGER` 로 깨우면 `kevent` 가 돌아와야 하고, 곧바로 다시 물으면 0개여야 한다. 이어서 20 ms 주기 `EVFILT_TIMER` 로 틱 3번을 받는다.

<details><summary>답</summary>

`EV_SET(&ev, 1, EVFILT_USER, 0, NOTE_TRIGGER, 0, NULL)` 을 `kevent` 로 보내면 깨어난다. wake 파이프와 달리 fd 가 필요 없고 읽어서 비울 바이트도 없다. 리눅스에서는 eventfd 가 같은 일을 한다(OS-M18). 타이머의 `data` 는 기본이 ms 다. 돌아온 이벤트의 `data` 는 지난번 이후 만료 횟수라서 루프가 늦어도 틱을 잃지 않는다. 그래서 테스트는 `ticks += data` 로 센다. 이벤트 루프의 타임아웃 · 유휴 연결 정리는 이 타이머로 만든다(타이머 휠은 NET-M09).
</details>

### 연습 4 — 플랫폼 사실을 테스트로 고정하기

이 레슨이 기댄 macOS 사실 세 가지를 CHECK 로 박아 둔다. ① strict POSIX 빌드에서 `select(1025, …)` → `EINVAL`. ② accept 한 소켓이 `O_NONBLOCK` 을 물려받음. ③ 상대가 닫은 소켓에 계속 쓰면 `EPIPE`(SIGPIPE 무시 상태).

<details><summary>답</summary>

실측은 세 가지 모두 통과했다. ③의 출력은 `write after peer close -> Broken pipe` 였다. 첫 쓰기는 성공하고 상대가 RST 를 돌려준다. 그 뒤의 쓰기가 EPIPE 다(ECONNRESET 일 수도 있어서 둘 다 허용). 이런 "환경 사실 테스트"를 두면 OS · SDK 를 올렸을 때 가정이 깨진 것을 바로 안다. ①은 `_DARWIN_C_SOURCE` 를 켜면 바뀐다(demo_fdset.c 출력: `-> 0 (ok)`). ②는 리눅스에서 반대다. 이식 코드에서는 accept 후 플래그를 항상 명시적으로 설정한다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread practice.c -o pr_san && ./pr_san
cc -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o pr_tsan && ./pr_tsan
```

실측: 세 빌드 모두 `17/17 passed`, ASan/UBSan 보고 0, TSan 경고 0 이었다.

## 10. 핵심 정리

- 세 모델의 차이는 **연결 상태가 어디 사는가**다. 스레드 모델은 스택, 이벤트 모델은 `conn_t` 다. 이벤트 처리기는 절대 블록하지 않는다(OSTEP 33.5).
- `write` 는 짧게 끝날 수 있다. 블로킹이면 `write_all` 루프를 돌고, 논블로킹이면 남은 출력을 들고 쓰기 알림을 기다린다. 출력이 남았으면 읽지 않는다(**역압** → 연결당 메모리 고정).
- select 는 fd 번호 < 1024, 매 호출 집합 재구성, O(maxfd) 검사가 한계다. fd ≥ 1024 를 `FD_SET` 하면 이 맥에서는 무시되거나 메모리를 덮는다(실측). accept 직후 가드로 거절한다.
- kqueue 는 관심을 커널에 등록해 두고(바뀔 때만 `kevent`), 준비된 것만 받는다. `udata` 로 연결을 바로 찾는다. `EVFILT_WRITE` 는 출력이 있을 때만 켠다.
- 레벨은 남아 있으면 또 알리고, EV_CLEAR/EPOLLET 는 새로 와야 알린다 → EAGAIN 까지 drain 한다. 쓰기를 끝낸 직후에도 drain 한다.
- 실측(1000 연결, 부하 걸린 M6): 연결당 32 KB vs 16 KB, 스레드 1002 vs 2, 서버 CPU 1.43 vs 0.78 s. 전부 바쁠 땐 처리량이 같았다. 10/1000 활성에선 select 40k vs kqueue 80–90k msgs/s 였다.
- macOS 사실: backlog 는 128 로 잘리고(넘치면 RST), accept 는 `O_NONBLOCK` 을 상속하고, `ru_maxrss` 는 바이트다. SIGPIPE 는 반드시 무시한다.

## 참고자료

- OSTEP 33장 *Event-based Concurrency (Advanced)*: 33.2–33.3 select, 33.5 블로킹 금지, 33.7 상태 관리(continuation), 33.8 남은 어려움. 로컬 `operating-system-three-easy-pieces/book-md/C33_event_based_concurrency_advanced.md`, <https://pages.cs.wisc.edu/~remzi/OSTEP/threads-events.pdf>
- macOS `man 2 kqueue` / `kevent` (이 맥에서 읽음): `EV_CLEAR`, `EV_ENABLE`/`EV_DISABLE`, `EV_DELETE` 자동 삭제, `EVFILT_READ` 의 `data` = 읽을 바이트 수, `EVFILT_WRITE`, `EV_EOF`
- macOS `man 2 select` (이 맥에서 읽음): `FD_SETSIZE`, "undefined if ... >= FD_SETSIZE", `EINVAL` when `nfds > FD_SETSIZE` without `_DARWIN_UNLIMITED_SELECT`. SDK 헤더 `sys/select.h`, `sys/_types/_fd_def.h`(`__darwin_check_fd_set_overflow`)
- macOS `man 2 listen` BUGS: backlog 128 로 제한, `man 2 getrusage`: `ru_maxrss` 바이트 단위
- POSIX.1-2017 `select`/`pselect`: <https://pubs.opengroup.org/onlinepubs/9699919799/functions/select.html> · `poll`: <https://pubs.opengroup.org/onlinepubs/9699919799/functions/poll.html> · `write`(부분 쓰기, EAGAIN): <https://pubs.opengroup.org/onlinepubs/9699919799/functions/write.html>
- Linux man-pages `accept(2)` NOTES (O_NONBLOCK 비상속): <https://man7.org/linux/man-pages/man2/accept.2.html> · `epoll(7)`: <https://man7.org/linux/man-pages/man7/epoll.7.html> · `select(2)`: <https://man7.org/linux/man-pages/man2/select.2.html>
- J. Lemon, "Kqueue: A generic and scalable event notification facility", USENIX ATC FREENIX 2001 (kqueue 설계 원전, 기억에 기댄 서지)
- D. Kegel, "The C10K problem": <http://www.kegel.com/c10k.html>
- lwIP raw/netconn/socket API 개요: <https://www.nongnu.org/lwip/> (Q7 의 lwIP 세부는 기억에 기댄 것, 버전별 확인 필요)
- 관련 레슨: OS-B04 (스레드), OS-B07 (시그널 · self-pipe), OS-B14 (IPC 비교), OS-M02 (문맥 교환 비용), OS-M13 (epoll LT/ET), OS-M18 (eventfd · signalfd · pidfd), OS-A11 (kqueue 데몬 + 워커 풀), NET-B09 (backlog · half-close), NET-B10 (메시지 프레이밍), NET-M09 (타이머 휠 · 백프레셔 · C10K), NET-A07 (thundering herd), LLAT-A03 (io_uring), BM-M04 (UART 링버퍼 TX 인터럽트), RF-M08 (AT 명령), C-M14 (ISR vs main)
