---
id: OS-M13
time: 90분
updated: 2026-11-11
---
# OS-M13 epoll 에코 서버: level vs edge triggered

## 학습 목표

- epoll 의 **LT(level-triggered)** 와 **ET(edge-triggered, `EPOLLET`)** 가 "언제 다시 알려 주는가" 를 같은 데이터로 실측해 비교하고, ET 에서 `EAGAIN` 까지 읽어야 하는 이유를 한 문장으로 말할 수 있다.
- 비차단 소켓 + 출력 버퍼 + "보낼 게 남았을 때만 `EPOLLOUT`" 으로 부분 쓰기(partial write)를 처리하는 에코 서버를 짜고, `EPOLLOUT` 을 늘 켜 두면 생기는 헛 깨어남을 숫자로 보인다.
- "ET 에서 이벤트당 read 한 번" 버그를 **매번 똑같이** 재현하고(8192 B 에서 멈춤), 새 edge 가 와야만 조금씩 풀리는 모습을 설명한다.
- `EPOLLONESHOT` 워커 풀에서 "처리 중인 fd 는 아무 워커에게도 다시 안 간다" 를 강제 스케줄로 검사하고, 재무장(`EPOLL_CTL_MOD`) 직후의 소유권 넘김 함정(TSan 이 실제로 잡음)을 설명한다.
- thundering herd 와 `EPOLLEXCLUSIVE`, kqueue(OS-M12) 와의 대응 관계를 면접에서 정리해 말할 수 있다.

## 1. 문제

"epoll 의 LT 와 ET 차이는? ET 에서 왜 EAGAIN 까지 읽어야 하나?" 는 리눅스 시스템/임베디드 리눅스 면접의 단골이고, 임베디드 리눅스 보드의 데몬(센서 게이트웨이, MAVLink 라우터, CAN↔이더넷 브리지, 카메라 메타데이터 서버)이 거의 다 epoll 이벤트 루프 위에서 돈다. 틀린 ET 코드는 **테스트에서는 잘 돌다가** 큰 메시지나 느린 클라이언트를 만나면 조용히 멈춘다 — 이 레슨은 그 멈춤을 결정적으로 재현하고 고친다. OS-M12 에서 맥의 kqueue 로 만든 에코 서버를 리눅스 epoll 로 옮긴다(이 파일만으로 완결되게 축약해 다시 썼다). eventfd 는 서버를 멈추는 데만 쓰고 자세한 건 OS-M18, 시그널 self-pipe 는 OS-B07, 서비스 데몬 전체 구조는 OS-A11 이다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 도착 순서, 다른 알림: 서버는 이벤트마다 4096 B 만 읽는다 (READ_SZ)</text>
  <text x="10" y="58" fill="currentColor">커널 수신 버퍼</text>
  <line x1="130" y1="80" x2="730" y2="80" stroke="currentColor"/>
  <rect x="150" y="50" width="70" height="30" class="f-mut" stroke="currentColor"/>
  <text x="152" y="45" fill="currentColor" font-size="12">조각1 10000</text>
  <rect x="220" y="60" width="160" height="20" class="f-mut" stroke="currentColor"/>
  <text x="232" y="74" fill="currentColor" font-size="12">5904 남음</text>
  <rect x="380" y="38" width="80" height="42" class="f-warn" stroke="currentColor"/>
  <text x="372" y="34" fill="currentColor" font-size="12">조각2 → 15904</text>
  <rect x="460" y="48" width="270" height="32" class="f-warn" stroke="currentColor"/>
  <text x="480" y="68" fill="currentColor" font-size="12">11808 남음 — 아무도 안 깨운다</text>
  <text x="10" y="120" fill="currentColor">LT 알림</text>
  <g fill="currentColor"><circle cx="160" cy="116" r="6" class="f-ok"/><circle cx="230" cy="116" r="6" class="f-ok"/><circle cx="300" cy="116" r="6" class="f-ok"/><circle cx="390" cy="116" r="6" class="f-ok"/><circle cx="470" cy="116" r="6" class="f-ok"/><circle cx="540" cy="116" r="6" class="f-ok"/><circle cx="610" cy="116" r="6" class="f-ok"/></g>
  <text x="640" y="120" fill="currentColor" font-size="12">… 빌 때까지</text>
  <text x="10" y="160" fill="currentColor">ET 알림</text>
  <circle cx="160" cy="156" r="6" class="f-acc" stroke="currentColor"/>
  <circle cx="390" cy="156" r="6" class="f-acc" stroke="currentColor"/>
  <text x="440" y="160" fill="currentColor" font-size="12">도착할 때만 (버퍼가 비었든 아니든)</text>
  <text x="10" y="200" fill="currentColor">버그판 ET</text>
  <text x="130" y="200" fill="currentColor" font-size="12" font-family="monospace">read 4096 → 에코 4096        read 4096 → 에코 8192 … 그리고 멈춤</text>
  <text x="10" y="236" fill="currentColor">정답 ET</text>
  <text x="130" y="236" fill="currentColor" font-size="12" font-family="monospace">read 4096, 4096, 1808, EAGAIN   read 4096, 4096, 1808, EAGAIN → 20000 전부</text>
  <text x="10" y="272" fill="currentColor">ET 의 약속: "EAGAIN 을 볼 때까지는 계속 준비된 것으로 여겨라" (epoll(7))</text>
  <text x="10" y="292" fill="currentColor">실측: 버그판은 8192 B 에서 멈춤, 1 B 더 보내야(새 edge) 12288 B — 5 회 모두 같음.</text>
</svg>
```

**환경 (이 레슨의 모든 숫자)**: Lima VM `elinux` (vz, aarch64, 6 vCPU, 12 GiB) · Ubuntu 26.04.1 LTS · kernel `7.0.0-38-generic` · glibc 2.43 · gcc 15.2.0 · clang 21.1.8 · strace 6.19. 호스트 Apple M6, 측정 당시 맥 load average **14.1 / 15.6 / 13.1** (다른 세션들이 같이 돌았다), VM 안 load average 0.06–0.35. VM vCPU 는 물리 코어가 아니니 숫자는 **비율**로만 읽는다. epoll 은 리눅스 전용이라 맥에서는 빌드되지 않는다(`/* ENV: linux */`).

### Part A — LT 에코 서버와 쓰기 쪽

단일 스레드, 리스닝 소켓 `127.0.0.1:0`(포트는 `getsockname`), 연결은 `accept4(SOCK_NONBLOCK)`. LT 에서는 **이벤트당 read 한 번**(`read_once`, 주어진 코드)이면 충분하다 — 남은 바이트가 있으면 epoll 이 다음 `epoll_wait` 에서 또 알려 준다.

- **TODO A1** `flush_out(s, c)` — 출력 버퍼를 `send(..., MSG_NOSIGNAL)` 로 보낼 수 있는 만큼. `EAGAIN` 이면 남겨 두고 0, 다 보냈으면 버퍼를 비운다. 연결 오류는 -1.
- **TODO A2** `set_interest(s, c)` — 원하는 관심 집합 = `base_events | (남았으면 EPOLLOUT)`. 지금 등록된 것과 같으면 **아무것도 안 한다**, 다르면 `EPOLL_CTL_MOD`.
- 테스트: 8 동시 연결 × {1, 100, 4096, 65536, 300000} B 바이트 일치, 다 보낸 뒤에야 읽는 느린 클라이언트(300000 B)에서 `write_eagain > 0` 과 `EPOLLOUT` 켜기/끄기(`mods ≥ 2`), 에코가 끝난 연결을 100 ms 놀려 둘 때 서버 깨어남 **0 번**, fd 누수 0 (`fcntl(F_GETFD)` 스캔).

### Part B — ET: EAGAIN 까지 읽기 vs 버그판

- **TODO B** `drain_read(s, c)` — `EAGAIN` 이 나올 때까지 읽어 출력 버퍼에 쌓는다. `EINTR` 은 다시, `0` 은 EOF.
- 모드 `M_ET` 는 `drain_read`, 모드 `M_ET_BUGGY` 는 같은 서버에서 읽기만 `read_once` (이벤트당 한 번).
- 재현 `two_chunk_probe`: 클라이언트가 10000 B 를 보내고 30 ms 뒤 또 10000 B, 그리고 20000 B 에코를 기다린다(300 ms 조용하면 멈춘 것으로 본다). 정답 ET·LT 는 20000, 버그판은 그보다 적다. 이어서 1 B 를 더 보내 "새 edge 가 와야 조금 풀린다" 를 확인한다. 테스트는 멈추지 않고 보고한다.

### Part C — EPOLLONESHOT 워커 풀

워커 4 개가 **epfd 하나**를 공유해 `epoll_wait` 한다. 연결 fd 는 `EPOLLIN | EPOLLET | EPOLLONESHOT` — 이벤트 하나가 나가는 순간 커널이 그 fd 를 꺼서, 받은 워커가 연결을 독점한다.

- **TODO C** `pool_handle(s, c, ev)` — drain → flush → (테스트 훅) → 실패면 닫기, 아니면 **재무장** `EPOLL_CTL_MOD`. 재무장하는 순간 다른 워커가 그 연결을 쥘 수 있으므로 "무엇을 언제까지 만져도 되나" 가 핵심이다.
- 테스트: 16 동시 연결 × 5 크기, 느린 클라이언트, 겹친 처리(`overlaps`) 0. **강제 스케줄**: 첫 처리 워커를 "처리 중" 에 붙잡아 둔 채 같은 fd 로 데이터를 더 보내고 100 ms 동안 **새 이벤트가 0 개**인지, 놓아준 뒤 재무장으로 나머지가 오는지 검사한다. ONESHOT 이 없을 때의 중복 배달은 `demo_pool.c` 가 센다(검사는 ONESHOT 쪽만 결정적으로).
- `EPOLLOUT` 은 보낼 게 남았을 때만, thundering herd 와 `EPOLLEXCLUSIVE` 는 `demo_semantics.c` 로 관찰.

## 2. Problem (English)

> Port a kqueue echo server to Linux epoll and run it in three modes.
>
> ```c
> server_t *server_create(int mode, int nworkers, int sndbuf); // M_LT, M_ET, M_ET_BUGGY, M_POOL; binds 127.0.0.1:0
> int  server_run(server_t *s);                               // starts 1 loop thread or nworkers pool workers
> void server_stop(server_t *s);                              // eventfd wake-up, join, close every fd
> int  flush_out(server_t *s, conn_t *c);       // send pending bytes until EAGAIN; 0 or -1
> int  set_interest(server_t *s, conn_t *c);    // EPOLLOUT only while output is pending; skip MOD if unchanged
> int  drain_read(server_t *s, conn_t *c);      // ET: read until EAGAIN; 1, 0 (EOF) or -1
> void pool_handle(server_t *s, conn_t *c, uint32_t ev); // EPOLLONESHOT worker: handle, then re-arm with MOD
> ```
>
> Constraints: non-blocking sockets only; never block the loop; echo must be byte-exact for messages of
> 1, 100, 4096, 65536 and 300000 bytes with 8–16 concurrent clients, including a client that does not read
> until it has sent everything (forces partial writes). An idle connection must cause zero wake-ups.
> In pool mode no two workers may process the same connection at the same time.
> Example: client sends 10000 B, then 10000 B more and waits. LT and correct ET echo 20000 B;
> an ET server that reads only 4096 B per event echoes 8192 B and stalls.
> Follow-ups: why does `EPOLL_CTL_MOD` "rescue" a stalled ET fd? What does EPOLLONESHOT protect, and what does it
> not? What is the thundering-herd problem and what do `EPOLLEXCLUSIVE` and `SO_REUSEPORT` change?

## 3. 시작 코드

<!-- code: starter.c -->

**리눅스 전용**. 맥 홈은 VM 에 **읽기 전용**으로 마운트되므로 VM 의 `/tmp/OS-M13/` 로 복사해 빌드한다. 첫 3 줄의 `/* BUILD: -pthread */` 대로 `-pthread` 가 필요하다.

```bash
limactl shell elinux                      # 이후 VM 안
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/OS/OS-M13
mkdir -p /tmp/OS-M13 && cp $L/*.c /tmp/OS-M13/ && cd /tmp/OS-M13
gcc   -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o st && ./st
clang -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o st && ./st
```

`accept4`, `SOCK_NONBLOCK` 은 `-std=c11` 에서 `#define _GNU_SOURCE` 를 include 앞에 둬야 보인다. 시작 코드 실행 (실제 출력 일부, 5.0 s — 모든 대기에 "진척 없음 300 ms" 상한이 있어 멈추지 않는다):

```text
== Part A: LT 에코 서버 (EPOLLOUT 은 보낼 게 남았을 때만) ==
PASS  s != NULL && server_run(s) == 0 && s->port > 0
FAIL  run_clients(s->port, 8) == 8  (line 510)
...
  ET-buggy: 10000 B x 2 보냄 → 에코 0 B 에서 멈춤 (300 ms 조용), 1 B 더 보내니 → 누적 0 B
...
  gate: 붙잡힌 동안 새 이벤트 0 개, 놓아준 뒤 에코 0/2000 B
FAIL  at && sent_b && e1 == e0 && STV(s, overlaps) == 0  (line 574)
FAIL  n == 2000 && memcmp(rx, tx, 2000) == 0  (line 575)
PASS  count_fds() == fd0
11/23 passed
```

(PASS 11 개는 "fd 누수 0", "놀 때 깨어남 0" 처럼 빈 스텁이 우연히 만족하는 것들이다.)

## 4. 힌트

<details>
<summary>힌트 1 — flush_out: send 의 세 가지 결과</summary>

`n > 0` 이면 `off += n` 하고 계속, `EINTR` 은 다시, `EAGAIN` 이면 "커널 송신 버퍼가 찼다" → 남은 건 버퍼에 두고 0 을 돌려준다(그다음은 `set_interest` 가 `EPOLLOUT` 을 켠다). 그 밖의 오류(`EPIPE`, `ECONNRESET`)는 -1. `write()` 대신 `send(..., MSG_NOSIGNAL)` — 상대가 닫은 소켓에 쓰면 `SIGPIPE` 로 **프로세스가 죽는다**.

</details>

<details>
<summary>힌트 2 — set_interest: "바뀔 때만" MOD</summary>

`want = base_events(s) | (obuf_pending(&c->out) ? EPOLLOUT : 0)`. `want == c->events` 면 return. 다르면 `c->events = want` 후 `EPOLL_CTL_MOD`. "같으면 안 한다" 는 syscall 절약이기도 하지만, ET 에서는 의미가 더 있다 — MOD 는 준비 상태를 다시 검사해서 이벤트를 새로 만들 수 있다(5. 풀이 E2). 버그판 ET 가 "운 좋게" 구출되지 않게 하려면 필요 없는 MOD 를 하지 않아야 한다.

</details>

<details>
<summary>힌트 3 — drain_read: 루프의 끝은 EAGAIN 뿐</summary>

`for (;;) { 자리 확보; n = read(...); n > 0 → 쌓고 continue; n == 0 → return 0; EINTR → continue; EAGAIN → return 1; 그 밖 → return -1; }`. `n < READ_SZ` 에서 멈추는 최적화는 스트림 소켓이면 epoll(7) Q&A 가 허용하지만, 이 레슨은 EAGAIN 까지 가는 정석을 쓴다(5. 풀이 Part B 에서 비용을 잰다).

</details>

<details>
<summary>힌트 4 — pool_handle: 재무장 뒤에는 c 를 만지지 마라</summary>

`EPOLL_CTL_MOD` 가 커널 안에서 끝나는 순간 다른 워커가 이벤트를 받아 그 연결을 쥘 수 있다. 그래서 fd 와 `want` 는 **busy 를 내리기 전에** 지역 변수로 계산한다. 연결을 닫는 쪽은 "아직 MOD 에서 돌아오지 않은 이전 주인" 이 없는지(`rearming == 0`) 확인한 다음 닫는다. 왜 0/1 플래그가 아니라 **카운터**여야 하는지는 5. 풀이 Part C.

</details>

## 5. 풀이

### 0. epoll 계약 — 이 레슨에 필요한 만큼

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">epoll 인스턴스 = 관심 목록(interest list) + 준비 목록(ready list)</text>
  <rect x="20" y="40" width="210" height="120" class="f-mut" stroke="currentColor"/>
  <text x="30" y="60" fill="currentColor" font-weight="bold">interest list (epitem)</text>
  <text x="30" y="82" fill="currentColor" font-size="12" font-family="monospace">fd 7: IN|ET</text>
  <text x="30" y="100" fill="currentColor" font-size="12" font-family="monospace">fd 8: IN|OUT</text>
  <text x="30" y="118" fill="currentColor" font-size="12" font-family="monospace">fd 9: IN|ET|ONESHOT</text>
  <text x="30" y="146" fill="currentColor" font-size="12">epoll_ctl ADD/MOD/DEL</text>
  <rect x="290" y="40" width="190" height="120" class="f-ok" stroke="currentColor"/>
  <text x="300" y="60" fill="currentColor" font-weight="bold">ready list</text>
  <text x="300" y="84" fill="currentColor" font-size="12">소켓 콜백이 epitem 을 건다</text>
  <text x="300" y="104" fill="currentColor" font-size="12">(데이터 도착, 송신 버퍼 여유)</text>
  <text x="300" y="130" fill="currentColor" font-size="12">MOD 도 지금 상태를 검사해</text>
  <text x="300" y="148" fill="currentColor" font-size="12">준비됐으면 건다 (E2)</text>
  <rect x="540" y="40" width="180" height="120" class="f-acc" stroke="currentColor"/>
  <text x="550" y="60" fill="currentColor" font-weight="bold">epoll_wait</text>
  <text x="550" y="84" fill="currentColor" font-size="12">목록에서 꺼내 poll 로 확인</text>
  <text x="550" y="106" fill="currentColor" font-size="12">LT: 다시 목록에 건다</text>
  <text x="550" y="126" fill="currentColor" font-size="12">ET: 걸지 않는다</text>
  <text x="550" y="146" fill="currentColor" font-size="12">ONESHOT: 마스크를 끈다</text>
  <path d="M230 100 L290 100" stroke="currentColor"/><path d="M480 100 L540 100" stroke="currentColor"/>
  <path d="M630 160 C630 200 385 200 385 160" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="430" y="214" fill="currentColor" font-size="12">LT 재삽입 → 다음 epoll_wait 도 보고</text>
  <text x="10" y="242" fill="currentColor" font-size="12">aarch64 에선 epoll_wait() 가 epoll_pwait syscall 로 들어간다 (strace 실측).</text>
</svg>
```

`demo_semantics.c` 로 man page 문장을 하나씩 확인했다 (Unix 소켓쌍, `epoll_wait(.., 0)` = 기다리지 않고 지금 준비된 수):

```text
E1 LT: 10000 B 도착, 안 읽고 epoll_wait(0) x5 → 1 1 1 1 1 | 100 B 만 읽고 → 1 | 상대가 1 B 더 → 1 1
E1 ET: 10000 B 도착, 안 읽고 epoll_wait(0) x5 → 1 0 0 0 0 | 100 B 만 읽고 → 0 | 상대가 1 B 더 → 1 0 | E2: 같은 마스크로 MOD → 1 0
E3 ONESHOT(LT): 10 B → 1 0 | 10 B 더 → 0 | MOD 재무장 → 1 0
E4 놀고 있는 연결 100 ms, 관심 = EPOLLIN          → 깨어남 0 번
E4 놀고 있는 연결 100 ms, 관심 = EPOLLIN|EPOLLOUT → 깨어남 375957 번
```

| 관찰 | 뜻 |
|---|---|
| LT `1 1 1 1 1` | LT = "지금 읽을 게 있나" (상태). 안 읽으면 계속 알린다. poll(2) 과 같은 의미. |
| ET `1 0 0 0 0`, 일부만 읽어도 `0` | ET = "새로 도착했다" (사건). 읽는 행위는 edge 를 만들지 않는다. |
| ET 에서 1 B 도착 → `1` | 버퍼가 비어 있지 않았어도 **도착마다** edge. "빈→참" 전이만이 아니다 (epoll(7): "an event will be generated upon each receipt of a chunk of data"). |
| E2 MOD → `1` | MOD 는 준비 상태를 다시 검사한다. ET 재무장, ONESHOT 재무장, 배압 재개(연습 2)가 모두 이것에 기댄다. |
| E3 ONESHOT | 한 번 알리면 마스크가 꺼져 새 데이터에도 조용, MOD 로 다시 켠다. |
| E4 375957 번 | LT 에 `EPOLLOUT` 을 켜 두면 송신 버퍼에 여유가 있는 한 매번 준비됨 → 바쁜 루프. |

### Part A — LT: 읽기 한 번, 쓰기는 버퍼 + EPOLLOUT 토글

에코 서버의 상태 기계는 "읽은 것을 출력 버퍼에 붙이고, 보낼 수 있을 때 보낸다" 뿐이다. 한 이벤트 처리 순서 (`on_conn_event`, 주어진 코드):

1. `EPOLLIN|HUP|ERR` 이면 읽기 (LT: `read_once`, ET: `drain_read`) — 읽은 바이트는 곧바로 출력 버퍼 끝에.
2. **무조건 `flush_out`** — `EPOLLOUT` 이벤트든 `EPOLLIN` 이벤트든 일단 보내 본다. 대부분 한 번에 다 나가고, 그러면 `EPOLLOUT` 은 켤 필요도 없다.
3. `set_interest` — 남았으면 `EPOLLOUT` 켜기, 다 나갔으면 끄기. **바뀔 때만** MOD.

| 출력 버퍼 | 등록 마스크 (LT) | 의미 |
|---|---|---|
| 비었음 | `EPOLLIN` | 읽을 게 오면 깨워 줘 |
| 남음 (`send` 가 EAGAIN) | `EPOLLIN \| EPOLLOUT` | 송신 버퍼에 자리가 나면도 깨워 줘 |
| 다시 비었음 | `EPOLLIN` | `EPOLLOUT` 을 안 끄면 E4 처럼 초당 수백만 번 깨어난다 |

느린 클라이언트가 확실히 부분 쓰기를 일으키도록 서버 쪽 소켓에 `SO_SNDBUF = 16384` 를 준다 (리눅스는 두 배인 32 KB 로 잡는다 — socket(7)). 300000 B 는 서버 송신 버퍼 + 클라이언트 수신 창보다 크므로 서버는 반드시 `EAGAIN` 을 만나고, 나머지는 사용자 공간 출력 버퍼에서 기다린다. 한 번 실행한 서버 통계(실측, 실행마다 조금씩 다르다):

```text
  [LT] wakeups 676 events 861 reads 824 writes 367 write_eagain 512 mods 44 accepts 10 overlaps 0
  [ET] wakeups 44 events 98 reads 825 writes 90 write_eagain 38 mods 30 accepts 10 overlaps 0
```

같은 일(읽기 성공 825 번 ≈ 같은 바이트)을 LT 는 676 번 깨어나서, ET 는 44 번 깨어나서 했다. LT 는 이벤트당 4096 B 씩 나눠 읽으며 루프를 여러 번 돌고, ET 는 한 번 깨어나 비울 때까지 읽는다. 어느 쪽이 빠른지는 작업 모양에 달렸다 — 아래 부하 측정에서 작은 메시지에서는 오히려 ET 가 느렸다.

### Part B — ET: 왜 EAGAIN 까지인가, 그리고 버그 재현

ET 의 계약은 epoll(7) 그대로다: **"이벤트를 받으면 그 fd 는 준비된 것으로 여겨라, 다음 (비차단) read/write 가 EAGAIN 을 낼 때까지."** 커널은 "새 데이터 도착" 에서만 epitem 을 준비 목록에 걸기 때문에, 버퍼에 남겨 둔 바이트는 다음 도착 전까지 아무도 알려 주지 않는다.

버그판(이벤트당 `read_once`)의 실측 (5 회 실행 모두 같은 값):

```text
  ET-buggy: 10000 B x 2 보냄 → 에코 8192 B 에서 멈춤 (300 ms 조용), 1 B 더 보내니 → 누적 12288 B
  [ET-buggy] wakeups 5 events 5 reads 4 writes 4 write_eagain 0 mods 0 accepts 1 overlaps 0
```

단계별 추론:

| 시점 | 커널 수신 버퍼 | 이벤트 | 서버 read | 누적 에코 |
|---|---|---|---|---|
| 조각1 도착 | 10000 | edge ① | 4096 | 4096 |
| (30 ms 대기) | 5904 | 없음 — 남겨 둔 건 알리지 않는다 | — | 4096 |
| 조각2 도착 | 15904 | edge ② | 4096 | 8192 |
| 300 ms 대기 | 11808 | 없음 → **멈춤** | — | 8192 |
| 1 B 더 | 11809 | edge ③ | 4096 | 12288 |

왜 결정적인가: 루프백에서 10000 B 를 `send` 한 번에 보내면 수신 쪽에 edge 가 하나 생기고, 버그판 서버는 이 실험 전체에서 read 를 4 번만 성공했다(`reads 4`). 혹시 한 조각이 두 세그먼트로 갈라져 edge 가 늘어도 CHECK 는 `got < 20000` 이라 깨지지 않는다 — 20000 을 다 읽으려면 edge 5 개가 필요하다. 그리고 `set_interest` 가 "바뀔 때만 MOD" 이므로 쓸데없는 MOD 가 버그를 몰래 구출하지도 않는다(E2: ET 에서 MOD 는 이벤트를 새로 만든다 — 실무 코드가 "매 이벤트마다 MOD" 를 하면 버그가 숨는다).

**ET 는 공짜가 아니다.** 1000 연결 × 64 B 왕복 20 번(`demo_load.c`)을 strace 로 세면 (서버 = `read`, 클라이언트 = `recvfrom` 이라 `read` 는 서버 것만):

```text
== lt   21006           read
== et   41006     20000 read          ← 20000 번이 EAGAIN 오류 (메시지당 한 번 더)
== pool 41006     20000 read  /  23799 epoll_ctl   ← ONESHOT 재무장 MOD ≈ 메시지당 한 번
```

작은 요청-응답에서 ET 는 "비었는지 확인하는 read" 를 메시지마다 한 번 더 한다. LT 는 그 확인을 커널의 다음 `epoll_wait` 에 맡긴다. 그래서 단일 스레드 비교에서 ET 가 LT 보다 느리게 나왔다 (아래 표). epoll(7) Q&A 는 스트림 소켓이면 "요청한 것보다 적게 읽혔다 = 비었다" 로 판단해도 된다고 적는다 — 이 최적화를 쓰면 이 비용이 사라지지만, 데이터그램·EOF·`EPOLLRDHUP` 경계에서 실수가 잦아 이 레슨은 정석만 쓴다.

`demo_load.c` (1000 연결, 왕복 200 번 × 3 회, 서버는 fork 한 자식 — RSS 는 자식의 `VmHWM`, CPU 는 `wait4`):

```text
LT             conns 1000  setup 131889 conn/s | RTT p50 3082.1 us p99 8985.0 us | 280278 msgs/s (200000 왕복) | server CPU 0.683 s, VmHWM:	    5924 kB
ET             conns 1000  setup 138617 conn/s | RTT p50 3998.3 us p99 7129.4 us | 245146 msgs/s (200000 왕복) | server CPU 0.810 s, VmHWM:	    6508 kB
ONESHOT pool4  conns 1000  setup 106491 conn/s | RTT p50 1183.3 us p99 5873.7 us | 590901 msgs/s (200000 왕복) | server CPU 1.031 s, VmHWM:	   12028 kB
LT             conns 1000  setup 135034 conn/s | RTT p50 2693.9 us p99 4921.0 us | 356355 msgs/s (200000 왕복) | server CPU 0.549 s, VmHWM:	    8572 kB
ET             conns 1000  setup 151810 conn/s | RTT p50 3483.2 us p99 4932.1 us | 281606 msgs/s (200000 왕복) | server CPU 0.696 s, VmHWM:	    8572 kB
ONESHOT pool4  conns 1000  setup 220353 conn/s | RTT p50 1097.6 us p99 8252.7 us | 596831 msgs/s (200000 왕복) | server CPU 1.008 s, VmHWM:	   12392 kB
LT             conns 1000  setup 151337 conn/s | RTT p50 2064.1 us p99 3575.7 us | 446931 msgs/s (200000 왕복) | server CPU 0.441 s, VmHWM:	    8572 kB
ET             conns 1000  setup 157723 conn/s | RTT p50 3515.4 us p99 6189.3 us | 275698 msgs/s (200000 왕복) | server CPU 0.704 s, VmHWM:	    8572 kB
ONESHOT pool4  conns 1000  setup 160799 conn/s | RTT p50 1161.9 us p99 4801.6 us | 640523 msgs/s (200000 왕복) | server CPU 0.963 s, VmHWM:	   12392 kB
```

읽는 법: 클라이언트가 1000 개 요청을 **동시에** 걸어 두는 닫힌 루프라서 RTT 는 대부분 줄 서는 시간이다 (1000 ÷ 28만 msgs/s ≈ 3.6 ms 와 맞는다). 세 번 모두 LT > ET (msgs/s 1.1–1.6 배), 워커 4 개 풀은 LT 의 1.4–2.1 배 msgs/s 를 내면서 CPU 도 1.5–2.2 배 썼다. RSS 는 연결 1000 개에 6–12 MB — 연결당 상태가 작은 이벤트 루프의 장점이다(스레드-per-연결과의 비교는 OS-M12). 공유 VM 위 숫자라 반복마다 30 % 넘게 흔들린다. 순서만 믿을 것.

### Part C — EPOLLONESHOT 워커 풀: 소유권 넘기기

워커 여럿이 epfd 하나에서 기다리면, ONESHOT 없이는 **같은 연결의 이벤트가 두 워커에게 동시에** 갈 수 있다 (LT 라면 처리 중에도 준비 상태라서, ET 라도 처리 중 새 데이터가 오면). 두 워커가 같은 출력 버퍼를 건드리면 데이터가 섞이고, 한쪽이 닫으며 해제하면 다른 쪽은 해제된 메모리를 쓴다. `demo_pool.c` 실측 (ONESHOT 없음 = LT 등록, 겹친 배달은 세고 그냥 돌아가게 막아 둠):

```text
① 강제 스케줄 — W1 이 A(1000 B) 를 읽고 붙잡힌 100 ms 동안 같은 fd 에 B(1000 B) 도착
  ONESHOT 없음 gate=1  붙잡힌 동안: 이벤트 315533, 깨어남 315533, 겹친 배달(overlaps) 315532 | 놓아준 뒤 에코 2000/2000 일치
  ONESHOT      gate=1  붙잡힌 동안: 이벤트 0, 깨어남 0, 겹친 배달(overlaps) 0 | 놓아준 뒤 에코 2000/2000 일치
② 자연 부하 — 32 연결 × 200 왕복 (64..4096 B), 워커 4
  ONESHOT 없음 성공 32/32, 0.03 s, wakeups 6004, events 13583, 겹친 배달 6510 (events 의 47.9%), reads 6400
  ONESHOT      성공 32/32, 0.02 s, wakeups 1320, events 6418, 겹친 배달 0 (events 의 0.0%), reads 6400
```

(두 번째 실행에서 ② 의 ONESHOT 없음은 겹친 배달 14679 / events 21730 = 67.6 %, ONESHOT 은 0 — 값은 흔들리고 0 인지 아닌지만 결정적이다.) 정답 코드의 강제 스케줄 CHECK 는 ONESHOT 쪽만 검사한다: `gate: 붙잡힌 동안 새 이벤트 0 개, 놓아준 뒤 에코 2000/2000 B` — 꺼진 fd 의 이벤트는 커널이 아예 만들지 않으니 시간과 무관하게 0 이다.

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">재무장 꼬리: W1 이 MOD 에서 아직 안 돌아왔는데 W2 가 이미 연결을 쥐었다</text>
  <text x="10" y="62" fill="currentColor">W1</text>
  <rect x="60" y="45" width="170" height="26" class="f-ok" stroke="currentColor"/>
  <text x="68" y="63" fill="currentColor" font-size="12">drain · flush (busy=1)</text>
  <rect x="230" y="45" width="70" height="26" class="f-mut" stroke="currentColor"/>
  <text x="236" y="63" fill="currentColor" font-size="12">busy-- </text>
  <rect x="300" y="45" width="260" height="26" class="f-warn" stroke="currentColor"/>
  <text x="308" y="63" fill="currentColor" font-size="12">epoll_ctl(MOD) … 커널에서 돌아오는 중</text>
  <text x="10" y="122" fill="currentColor">W2</text>
  <rect x="360" y="105" width="150" height="26" class="f-ok" stroke="currentColor"/>
  <text x="368" y="123" fill="currentColor" font-size="12">이벤트 → EOF 발견</text>
  <rect x="510" y="105" width="90" height="26" class="f-acc" stroke="currentColor"/>
  <text x="516" y="123" fill="currentColor" font-size="12">close · free</text>
  <path d="M355 71 L370 105" stroke="currentColor"/>
  <text x="380" y="96" fill="currentColor" font-size="12">MOD 가 커널에서 끝난 순간 이벤트</text>
  <text x="10" y="168" fill="currentColor" font-size="12">TSan (처음 버전): close(fd) by W2  vs  epoll_ctl(fd) by W1 — data race</text>
  <text x="10" y="190" fill="currentColor" font-size="12">0/1 플래그: W0 의 늦은 rearming=0 이 W1 의 1 을 덮음 → W2 가 안 기다리고 free (TSan)</text>
  <text x="10" y="216" fill="currentColor" font-weight="bold">정답: rearming 은 카운터</text>
  <text x="10" y="236" fill="currentColor" font-size="12" font-family="monospace">주인: rearming += 1; busy -= 1 (release); MOD; rearming -= 1 (release)</text>
  <text x="10" y="252" fill="currentColor" font-size="12" font-family="monospace">닫는 쪽: while (rearming != 0) yield;  close; free</text>
  <text x="10" y="268" fill="currentColor" font-size="12">꼬리는 여러 사이클 겹칠 수 있다 → "아직 MOD 안에 있는 수" 를 센다.</text>
</svg>
```

ONESHOT 이 보장하는 것은 **"이벤트가 한 번에 한 워커에게만 간다"** 까지다. 재무장한 워커가 `epoll_ctl` 에서 돌아오기 전에 다음 워커가 이벤트를 받는 것은 막지 못한다. 그래서 순서가 중요하다:

| 단계 | 정답 코드 | 이유 |
|---|---|---|
| ④ | `fd`, `want` 를 지역 변수로 먼저 계산 | `want` 는 `c->out` 을 읽는다 — busy 를 내린 뒤에 읽으면 다음 주인과 경합 |
| ⑤ | `rearming += 1`, 그다음 `busy -= 1` (release) | 다음 주인은 busy 의 acquire 로 "누가 아직 MOD 중" 임을 본다 |
| ⑥ | `MOD`, 그다음 `rearming -= 1` (release) | c 를 만지는 마지막 줄. 이 뒤로 c 는 해제될 수 있다 |
| ③ | 닫는 쪽은 `rearming == 0` (acquire) 까지 양보 후 `conn_close` | 이전 주인들이 모두 MOD 에서 돌아왔다 → fd 를 닫고 c 를 해제해도 안전 |

이 순서는 처음부터 이렇게 쓴 게 아니다. 처음 버전(MOD 뒤로 c 를 전혀 안 만짐)에서 clang TSan 이 실제로 이렇게 보고했다 (BuildId 와 스레드 생성 스택은 줄임):

```text
WARNING: ThreadSanitizer: data race (pid=261943)
  Write of size 8 at 0xfffff4800120 by thread T22:
    #0 close <null> (so_ts+0x7543c)
    #1 conn_close /tmp/OS-M13/solution.c:166:5
    #2 pool_handle /tmp/OS-M13/solution.c:244:19
  Previous read of size 8 at 0xfffff4800120 by thread T23:
    #0 epoll_ctl <null> (so_ts+0x76a74)
    #1 ep_ctl /tmp/OS-M13/solution.c:106:12
    #2 pool_handle /tmp/OS-M13/solution.c:248:9
  Location is file descriptor 9 created by thread T23 at:
```

커널 쪽은 아마 안전하다 (syscall 은 들어올 때 fd 를 파일 참조로 바꿔 쥐고, 이벤트는 재무장이 끝난 뒤에야 생긴다 — 일반적인 커널 fd 처리에서 한 추론이고 `fs/eventpoll.c` 를 직접 읽어 확인하지는 않았다). 하지만 "다른 스레드가 아직 쓰고 있는 fd 를 닫는다" 는 같은 fd 번호가 곧 새 연결에 재사용되는 서버에서는 위험한 습관이라, 고쳤다. 다음으로 0/1 플래그를 넣었더니 TSan 이 `free` vs 늦은 `rearming = 0` 저장을 또 잡았다 — 이전 주인 W0 의 꼬리가 W1 의 꼬리와 **겹쳐서**, W0 이 늦게 쓴 0 이 W1 의 1 을 덮어쓴 것이다. 카운터로 바꾼 뒤 TSan 8 회 연속 경고 0 (아래 검증). instrumented 빌드로 "재무장 없이 배달된 이벤트" 를 따로 세어 보니 0 이었다 — 즉 ONESHOT 자체는 지켜졌고, 문제는 사용자 공간의 소유권 넘김이었다.

### thundering herd 와 EPOLLEXCLUSIVE

스레드 4 개가 같은 listen 소켓을 기다릴 때 연결 하나에 몇 개가 깨어나나 (`demo_semantics.c` E5, 각 5 회):

```text
E5 스레드 4 개, 같은 epfd, LT            : 연결 1 개에 깨어난 스레드 수 (5 회) → 4 4 4 4 4
E5 스레드 4 개, 같은 epfd, ET            : 연결 1 개에 깨어난 스레드 수 (5 회) → 1 1 1 1 1
E5 스레드 4 개, 각자 epfd, 기본          : 연결 1 개에 깨어난 스레드 수 (5 회) → 4 4 4 4 4
E5 스레드 4 개, 각자 epfd, EPOLLEXCLUSIVE: 연결 1 개에 깨어난 스레드 수 (5 회) → 1 1 1 1 1
```

- 같은 epfd + LT: 꺼낸 스레드가 epitem 을 다시 걸고 다음 대기자를 깨우는 것이 연쇄돼 넷 다 깬다 (아무도 accept 하지 않아 계속 준비 상태). 같은 epfd + ET: epoll(7) 문장 그대로 "just one of the threads is awoken".
- 각자 epfd: 소켓의 대기 큐에 epoll 인스턴스 넷이 걸려 있어 넷 다 깬다. `EPOLLEXCLUSIVE` (Linux 4.5+, `EPOLL_CTL_ADD` 에서만 — MOD 에 쓰면 `EINVAL`) 는 "하나 **이상**" 만 깨운다 — man page 가 "one or more" 라고 적으므로 이 실측의 1 은 보장이 아니다.
- 서버 설계에서는 프로세스·스레드마다 listen 소켓을 따로 두는 `SO_REUSEPORT` 가 다른 길이다. 측정과 비교는 NET-A07 의 몫이라 여기선 이름만.
- 이 레슨의 풀은 listen fd 도 `EPOLLONESHOT` 으로 두고 accept 루프 뒤 재무장한다 → 한 번에 한 워커만 accept.

### kqueue(OS-M12) 와의 대응

| epoll (리눅스) | kqueue (macOS/BSD) | 메모 |
|---|---|---|
| 기본 LT | 기본 (상태 보고) | |
| `EPOLLET` | `EV_CLEAR` | 맥 `man 2 kqueue`: "After the event is retrieved by the user, its state is reset" |
| `EPOLLONESHOT` + `EPOLL_CTL_MOD` | `EV_DISPATCH` + `EV_ENABLE` | `EV_DISPATCH` 는 MacOSX26.4.sdk `sys/event.h` 에 "disable event after reporting" 로 있으나 맥 man page 엔 설명이 없다 |
| (없음) | `EV_ONESHOT` | kqueue 쪽은 보고 후 **삭제**. epoll ONESHOT 은 삭제가 아니라 비활성 |
| `epoll_ctl` 1 회 = 변경 1 개 | `kevent()` 변경 목록 + 대기를 한 번에 | 재무장 syscall 수 차이 (strace: 풀 23799 `epoll_ctl`) |
| 읽기/쓰기 = `EPOLLIN`/`EPOLLOUT` 마스크 | `EVFILT_READ`/`EVFILT_WRITE` 별개 필터 | kqueue 는 `data` 에 읽을 바이트 수도 준다 |

### 펌웨어 연결

- 임베디드 리눅스 게이트웨이 데몬(예: UART/CAN 수신 → TCP 로 중계)은 보통 단일 스레드 epoll 루프다. 센서 쪽 fd(`/dev/ttyS*`, SocketCAN — COMM-M09)도 같은 루프에 넣는다. CPU 가 하나뿐인 SoC 라면 LT + 이벤트당 한 번 읽기가 공정성(한 연결이 루프를 독점하지 않음)에서 오히려 낫다.
- RTOS 에는 epoll 이 없다. 대응물은 "ISR 이 데이터를 링 버퍼에 넣고 태스크에 알림" 인데, 알림이 **카운트되지 않는** 이진 세마포어/이벤트 플래그면 ET 와 같은 함정이 생긴다 — 태스크는 깨어나면 링 버퍼가 빌 때까지 꺼내야 한다 (BM-M04 의 UART 링 버퍼, RTOS-B11 큐).
- 메모리 제약: 출력 버퍼를 무제한으로 키우면 느린 클라이언트 하나가 RAM 을 다 먹는다 → 연습 2 의 배압(HIWAT/LOWAT). 타이머 휠·C10K 수준 설계는 NET-M09.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

검증 (VM, `/tmp/OS-M13/`): gcc·clang `-Wall -Wextra -Werror -O2 -pthread` 경고 0, **23/23 passed** 를 5 회 연속 (1.5 s), 다른 cwd(`cd / && /tmp/OS-M13/so`)에서도 23/23. gcc·clang `-fsanitize=address,undefined` 23/23 오류 0, clang `-fsanitize=thread` 23/23 경고 0 (3 회, 카운터 수정 후 별도로 8 회).

## 7. 자주 하는 실수

1. **ET 인데 한 번만 읽는다** → 남은 바이트는 다음 도착 전까지 아무도 안 알려 준다 (Part B 표: 8192 B 에서 멈춤). 고침: `drain_read` 처럼 EAGAIN 까지. listen 소켓도 같다 — ET 로 등록했으면 `accept` 를 EAGAIN 까지 (연습 1: 백로그에 7 개가 갇힘).
2. **`EPOLLOUT` 을 항상 켜 둔다 (LT)** → 놀고 있는 연결 하나로 100 ms 에 375957 번 깨어남 (E4). 고침: 출력이 남았을 때만 켜고, 다 보내면 끈다 (`set_interest`). ET 에서는 `EPOLLIN|EPOLLOUT` 을 처음부터 켜 두는 방식도 epoll(7) 이 소개한다 — edge 라서 바쁜 루프가 안 되기 때문.
3. **차단 소켓을 epoll 에 넣는다** → ET 의 "EAGAIN 까지" 루프가 마지막 read 에서 영원히 막혀 루프 전체가 선다. 고침: `accept4(..., SOCK_NONBLOCK)`, 클라이언트 쪽도 `O_NONBLOCK`.
4. **`write()` 로 보낸다** → 상대가 RST 로 끊은 소켓에 쓰면 `SIGPIPE` 로 데몬이 죽는다. 고침: `send(..., MSG_NOSIGNAL)` 또는 `signal(SIGPIPE, SIG_IGN)`.
5. **EOF 에서 바로 닫는다** → 상대가 `shutdown(SHUT_WR)` 만 한 경우 아직 보낼 에코가 남았는데 버린다 (연습 3 실측: 200000 B 중 0 B 수신). 고침: 읽기 끝 표시만 하고 출력이 다 나간 뒤 닫는다.
6. **ONESHOT 재무장 후에 c 를 만진다** (`c->events = want` 를 MOD 뒤에 쓰기, 통계를 c 에 쓰기 등) → 다음 워커와 경합, 최악엔 해제된 메모리. 고침: 필요한 값은 busy 를 내리기 전에 지역 변수로. 닫는 쪽은 `rearming` 카운터가 0 이 될 때까지 기다린다 (0/1 플래그는 꼬리가 겹치면 틀린다 — TSan 실측).
7. **close 하면 epoll 에서 빠진다고 믿는다** → 맞는 건 "그 열린 파일을 가리키는 fd 가 **모두** 닫혔을 때" 뿐이다 (epoll(7) Q&A). `dup`/`fork` 로 복제된 fd 가 남아 있으면 닫힌 fd 번호로 이벤트가 계속 온다. 고침: 닫기 전에 `EPOLL_CTL_DEL` (`conn_close` 처럼), `SOCK_CLOEXEC`.

## 8. 면접 Q&A

<details>
<summary>Q1. LT 와 ET 의 차이를 한 문장씩? 꼬리: ET 가 더 빠르다는 말은 맞나?</summary>

LT 는 "지금 읽을/쓸 수 있으면 매번 알린다"(poll 과 같은 의미), ET 는 "새 데이터가 도착하거나 송신 여유가 새로 생길 때만 알린다". 실측: 같은 10000 B 를 안 읽고 다섯 번 물으면 LT `1 1 1 1 1`, ET `1 0 0 0 0`. 꼬리: ET 는 깨어남 수를 줄이지만(Part A 통계 676 vs 44) EAGAIN 을 확인하는 read 가 메시지마다 하나 더 든다 — 1000 연결 64 B 왕복에서 strace `read` 21006 vs 41006(20000 EAGAIN), 처리량은 오히려 LT 가 1.1–1.6 배 높았다. "ET 가 빠르다" 는 작업 모양에 따라 다르다.

</details>

<details>
<summary>Q2. ET 에서 왜 EAGAIN 까지 읽어야 하나? 꼬리: n &lt; 요청량이면 멈춰도 되나?</summary>

ET 는 버퍼에 남은 데이터를 다시 알려 주지 않는다 — 이벤트는 "도착" 에서만 생긴다. 일부만 읽고 epoll_wait 로 돌아가면 상대가 더 보내기 전까지 영원히 기다릴 수 있고, 상대는 응답을 기다리니 교착이 된다 (재현: 20000 B 중 8192 B 에서 멈춤). 꼬리: epoll(7) Q&A 는 스트림 소켓·파이프라면 "요청보다 적게 읽혔다 = 비었다" 로 판단해도 된다고 적는다. 데이터그램이나 정규 모드 터미널은 EAGAIN 까지 가야만 안다. 짧게 읽힌 직후 도착한 데이터는 새 edge 를 만드니 놓치지 않는다.

</details>

<details>
<summary>Q3. EPOLLOUT 은 언제 켜나? 꼬리: ET 라면?</summary>

LT 에서는 출력이 남았을 때만 켜고 다 보내면 끈다 — 늘 켜 두면 송신 버퍼에 자리가 있는 한 매번 준비라 100 ms 에 375957 번 깨어났다. 쓰기는 먼저 `send` 를 해 보고 EAGAIN 일 때만 켠다(대부분 한 번에 다 나간다). 꼬리: ET 에서는 `EPOLLIN|EPOLLOUT` 을 처음부터 등록해도 바쁜 루프가 안 된다 (여유가 "새로 생길 때" 만 알림). epoll(7) 이 MOD 를 아끼는 방법으로 소개한다. 다만 출력이 없을 때 오는 EPOLLOUT edge 를 처리할 준비가 돼 있어야 한다.

</details>

<details>
<summary>Q4. EPOLLONESHOT 은 무엇을 막고 무엇을 막지 못하나?</summary>

막는 것: 한 fd 의 이벤트가 동시에 두 워커에게 가는 것. 이벤트 하나를 내보내는 순간 커널이 그 fd 의 마스크를 꺼서 `EPOLL_CTL_MOD` 로 다시 켤 때까지 아무 이벤트도 만들지 않는다 (강제 스케줄 실측: 붙잡힌 100 ms 동안 새 이벤트 0, ONESHOT 없으면 315533). 막지 못하는 것: 재무장한 워커가 아직 `epoll_ctl` 안에 있는데 다음 워커가 연결을 쥐는 것, 그리고 해제·close 의 소유권. 그래서 재무장 뒤엔 c 를 만지지 않고, 닫는 쪽은 재무장 중인 이전 주인 수(`rearming`)가 0 이 되길 기다린다. MOD 는 준비 상태를 다시 검사하므로 "EAGAIN 직후 ~ MOD 사이에 온 데이터" 도 놓치지 않는다.

</details>

<details>
<summary>Q5. thundering herd 란? EPOLLEXCLUSIVE 와 SO_REUSEPORT 는 어떻게 다른가?</summary>

이벤트 하나에 기다리던 여럿이 다 깨어나고 하나만 일을 얻는 낭비. 실측: 스레드 4 개가 각자 epfd 로 같은 listen 소켓을 보면 연결 하나에 4 개가 깨고, `EPOLLEXCLUSIVE` 면 1 개 (man page 보장은 "one or more"). 같은 epfd 를 공유하면 ET 는 1 개, LT 는 연쇄로 4 개. `EPOLLEXCLUSIVE` 는 같은 소켓을 여러 epoll 이 볼 때 깨울 대상을 줄이는 것이고, `SO_REUSEPORT` 는 listen 소켓 자체를 여러 개 만들어 커널이 연결을 해시로 나눠 주는 것 — 대기 큐가 아예 갈린다 (NET-A07 에서 측정 예정 — 여기선 실측하지 않음).

</details>

<details>
<summary>Q6. 워커 여럿이 epfd 하나를 공유하는 풀 vs 스레드마다 epfd 를 따로 두는 구조?</summary>

공유 epfd + ONESHOT: 바쁜 워커가 있으면 다른 워커가 자동으로 일을 가져가 부하가 고르게 퍼진다. 대가는 메시지마다 재무장 `epoll_ctl` 1 회 (strace 23799 회)와 이 레슨의 소유권 함정. 스레드마다 epfd(연결을 accept 때 한 스레드에 고정, nginx/memcached 류 구조): 재무장도, 연결 단위 동기화도 필요 없지만 한 스레드에 무거운 연결이 몰리면 불균형. 실측(워커 4, 64 B 왕복): 풀이 단일 스레드 LT 의 1.4–2.1 배 msgs/s, CPU 는 1.5–2.2 배 더 썼다.

</details>

<details>
<summary>Q7. 소켓을 close 하면 epoll 에서 자동으로 빠지나? 꼬리: 그러면 DEL 은 왜 하나?</summary>

열린 파일 설명(open file description)을 가리키는 fd 가 **모두** 닫혀야 빠진다 (epoll(7) Q&A). `fork` 나 `dup` 으로 복제된 fd 가 남아 있으면 이미 닫은 fd 번호로 이벤트가 계속 오고, 그 번호가 새 연결에 재사용되면 엉뚱한 연결 구조체를 건드린다. 그래서 닫기 전에 `EPOLL_CTL_DEL` 하고, 모든 fd 를 `SOCK_CLOEXEC`/`EPOLL_CLOEXEC` 로 만든다 (이 레슨의 `conn_close`). 이벤트의 `data` 에 포인터를 넣었다면 해제 전에 DEL 하는 것이 특히 중요하다.

</details>

<details>
<summary>Q8. kqueue 의 어떤 옵션이 EPOLLET / EPOLLONESHOT 에 해당하나? 꼬리: epoll 의 약점으로 자주 꼽히는 것은?</summary>

`EV_CLEAR` ≈ `EPOLLET`, `EV_DISPATCH` + `EV_ENABLE` ≈ `EPOLLONESHOT` + `EPOLL_CTL_MOD`, kqueue 의 `EV_ONESHOT` 은 보고 후 **삭제**라 epoll ONESHOT(비활성)과 다르다. 꼬리: ① 변경 하나에 syscall 하나 (`kevent` 는 변경 목록을 대기와 한 번에 넘긴다), ② 일반 파일은 epoll 에 넣을 수 없다 (`EPERM`) — 디스크 I/O 는 스레드 풀이나 io_uring (LLAT-A03), ③ fd 가 아니라 "열린 파일" 단위 등록이라 dup/fork 와 얽힌 함정 (Q7). 반대로 epoll 은 fd 수와 무관한 O(준비된 수) 대기라 select/poll 의 O(n) 스캔이 없다 (OSTEP 33장 맥락).

</details>

## 9. 추가 연습

`practice.c` 는 단일 스레드 ET 에코 서버를 이 파일 안에 다시 갖고(본문과 독립), 설정 구조체 `cfg_t` 로 기능을 켠다.

### 연습 1 — ET 로 등록한 listen 소켓

이벤트 루프가 돌기 전에 클라이언트 8 개가 연결을 끝내 둔다 (백로그). 루프가 시작되면 몇 개를 받나? `accept_loop=0`(이벤트당 accept 한 번)과 `=1`(EAGAIN 까지)을 비교하고, 9 번째 연결이 오면 무슨 일이 생기는지 보여라.

<details>
<summary>답</summary>

```c
static void do_accept(server_t *s) {
    do {
        int fd = accept4(s->lfd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) { if (errno == EINTR || errno == ECONNABORTED) continue; return; }
        /* ... conn 만들고 EPOLLIN|EPOLLET 로 ADD ... */
    } while (s->cfg.accept_loop);
}
```

실측: `accept_loop=0: 8 개 대기 → accepts 1, 9 번째 연결 뒤 → 2` / `accept_loop=1: → 8, → 9`. 연결 8 개가 와도 epitem 은 준비 목록에 한 번만 걸린다 → edge 하나에 accept 하나. 9 번째 연결의 edge 는 **가장 오래된** 대기 연결 하나를 더 꺼낼 뿐이다 — 나머지 7 개는 클라이언트 입장에선 연결됐는데 서버가 영원히 모른다. 부하가 몰릴 때만 나타나는 전형적 운영 장애다.

</details>

### 연습 2 — 배압: HIWAT / LOWAT

출력 버퍼가 64 KB(HIWAT) 이상이면 읽기를 멈추고(`EPOLLIN` 제거), 16 KB(LOWAT) 이하로 내려가면 재개하라. 쓰기 스레드는 계속 보내고 읽기는 100 ms 뒤에야 시작하는 느린 클라이언트로 300000 B 에코를 검사하고, 최대 출력 버퍼를 배압 끔과 비교하라.

<details>
<summary>답</summary>

`drain` 은 루프 맨 앞에서 `pending >= hiwat` 이면 `rd_paused = 1` 로 멈춘다. `on_event` 는 flush 후 `rd_paused && pending <= lowat` 이면 `rd_paused = 0`, 관심 집합은 `EPOLLET | (rd_paused ? 0 : EPOLLIN) | (pending ? EPOLLOUT : 0)` 이고 바뀔 때만 MOD. 핵심은 재개다: 멈춰 있던 동안 커널에 쌓인 데이터는 **새 edge 를 만들지 않는다**. 그런데 `EPOLLIN` 을 다시 넣는 MOD 가 준비 상태를 다시 검사하므로(E2) 이벤트가 바로 온다. 실측: `배압 켬: echo 300000 B, paused 5 resumed 5, 최대 출력 버퍼 67584 B` (= 64 KB + 2048, 상한 HIWAT + READ_SZ 안), `배압 끔: 최대 출력 버퍼 271328 B`. 배압이 켜지면 클라이언트의 send 가 막혀 TCP 흐름 제어가 상대를 늦춘다. 서버 메모리가 아니라 소켓 버퍼가 줄을 선다.

</details>

### 연습 3 — 반쪽 닫기 (half-close)

클라이언트가 200000 B 를 보내고 `shutdown(SHUT_WR)` 한 뒤 EOF 까지 읽는다. `read()==0` 에서 바로 닫는 서버와, 남은 에코를 다 보내고 닫는 서버를 비교하라.

<details>
<summary>답</summary>

`read()==0` 이면 `rd_eof = 1` 만 표시하고 계속한다. 관심 집합에서 `EPOLLIN` 을 빼고(`rd_eof ? 0 : EPOLLIN`) 출력이 남았으면 `EPOLLOUT` 으로 마저 보낸다. `rd_eof && pending == 0` 이 되면 닫는다. 실측: `halfclose=0: 보냄 200000, 받음 0, 정상 EOF 1` (EOF 를 같은 drain 에서 만나 flush 도 못 하고 닫음), `halfclose=1: 보냄 200000, 받음 200000, 정상 EOF 1`. HTTP/1.0 식 "요청 다 보냈음 = 쓰기 닫기" 클라이언트와 netcat 이 이렇게 동작한다.

</details>

### 연습 4 — 유휴 타임아웃

200 ms 동안 아무것도 안 보낸 연결을 서버가 닫게 하라. 타이머 fd 없이 `epoll_wait` 의 timeout 을 "가장 이른 마감까지 남은 시간" 으로 준다. 30 ms 마다 1 B 를 왕복하는 연결은 600 ms 동안 살아 있어야 한다.

<details>
<summary>답</summary>

`idle_sweep()` 이 매 루프 시작에 모든 연결을 훑어 마감이 지난 것은 닫고, 남은 것 중 최소 남은 시간을 돌려준다 → `epoll_wait(..., idle_sweep(s))`. 읽을 때마다 `last_ms` 갱신. 실측: `놀던 연결: 받음 0, EOF 1 (670 ms 경과) | 바쁜 연결 600 ms 동안 정상 1 | idle_closed 1` (경과 시간은 바쁜 연결 실험 600 ms 를 포함한 값). 선형 스캔은 연결이 많으면 O(n) — 실무는 마감 순 최소 힙이나 타이머 휠(NET-M09), 또는 timerfd 를 같은 epoll 에 넣는다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
# VM 안, /tmp/OS-M13/
gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr       # 10/10 passed (5 회 동일, 1.7 s)
clang -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o pr_t && ./pr_t  # 10/10, WARNING 0 (3 회)
```

ASan+UBSan(gcc, clang) 도 10/10, 오류 0. 데모 (모두 `/* ENV: linux */`, strict 플래그로 gcc·clang 빌드 확인, 출력은 5. 풀이에 붙였다):

```bash
gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_semantics.c -o sem  && ./sem
gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_pool.c      -o pool && ./pool    # solution.c 를 #include
gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_load.c      -o load && ./load 1000 200
strace -f -c -e trace=read,epoll_ctl ./load 1000 20 et                                # 모드 하나만
```

<!-- code: demo_semantics.c -->

<!-- code: demo_pool.c -->

<!-- code: demo_load.c -->

## 10. 핵심 정리

- LT = "지금 준비됐나"(poll 과 같은 의미, 안 읽으면 계속 알림), ET = "새로 도착했다"(도착마다 한 번, 읽기는 edge 를 만들지 않음). 실측 `1 1 1 1 1` vs `1 0 0 0 0`.
- ET 의 규칙: 이벤트를 받으면 **EAGAIN 까지** 읽고 쓴다. 한 번만 읽으면 20000 B 중 8192 B 에서 멈추고, 새 edge 가 와야 4096 B 씩 풀린다. listen 소켓의 accept 도 마찬가지.
- 쓰기: 먼저 `send(MSG_NOSIGNAL)` 해 보고, EAGAIN 일 때만 `EPOLLOUT` 을 켜고, 다 보내면 끈다. 늘 켜 두면(LT) 놀고 있는 연결에서 100 ms 에 37 만 번 깨어난다.
- `EPOLL_CTL_MOD` 는 준비 상태를 다시 검사한다 → ET/ONESHOT 재무장과 배압 재개가 데이터를 놓치지 않는 이유. 동시에 "매번 MOD" 가 ET 버그를 숨기는 이유.
- ET 가 늘 빠른 건 아니다: 메시지마다 EAGAIN 확인 read 가 하나 더 (strace 21006 vs 41006).
- `EPOLLONESHOT` = 한 이벤트 → 한 워커. 처리 후 MOD 로 재무장. 재무장 뒤엔 c 를 만지지 말고, 닫는 쪽은 아직 MOD 중인 이전 주인 수(카운터)가 0 이 되길 기다린다 (TSan 이 0/1 플래그 버전을 잡았다).
- thundering herd: 각자 epfd 면 4 개 다 깸 → `EPOLLEXCLUSIVE` 로 "하나 이상", 또는 `SO_REUSEPORT`. 같은 epfd 는 ET 1, LT 4 (실측).
- close 는 모든 복제 fd 가 닫혀야 epoll 에서 빠진다 → 닫기 전 `EPOLL_CTL_DEL`, 모든 fd 는 CLOEXEC.

## 참고자료

- `epoll(7)` — LT/ET 설명, "Possible pitfalls", Q&A (EAGAIN, starvation, close) — https://man7.org/linux/man-pages/man7/epoll.7.html (VM 의 man-pages 로 확인)
- `epoll_ctl(2)` — `EPOLLET`, `EPOLLONESHOT`, `EPOLLEXCLUSIVE` (since Linux 4.5, "one or more") — https://man7.org/linux/man-pages/man2/epoll_ctl.2.html
- `epoll_wait(2)`, `accept4(2)`, `send(2)` (`MSG_NOSIGNAL`), `socket(7)` (`SO_SNDBUF` 두 배), `eventfd(2)` — man7.org
- POSIX `send()` / `read()` — `EAGAIN`/`EWOULDBLOCK` 의미: https://pubs.opengroup.org/onlinepubs/9799919799/functions/send.html
- macOS `man 2 kqueue` (`EV_CLEAR`, `EV_ONESHOT`, `EV_ENABLE`) 과 MacOSX26.4.sdk `sys/event.h` (`EV_DISPATCH`) — 이 맥에서 읽음
- OSTEP 33장 "Event-based Concurrency (Advanced)" — select/poll 이벤트 루프, 차단 금지, 상태 관리 (https://pages.cs.wisc.edu/~remzi/OSTEP/threads-events.pdf; Don 의 노트 `notes/2026-09-30_C33_event_based_concurrency.md`)
- Linux 커널 소스 `fs/eventpoll.c` (ep_poll_callback, ep_send_events 의 LT 재삽입, ep_modify 의 재검사) — 동작 설명은 위 실측으로 확인한 범위만 적었다
- 이 저장소: OS-M12 (select/kqueue 에코 서버), OS-M18 (eventfd·signalfd·pidfd), OS-B07 (self-pipe), OS-A11 (서비스 데몬), NET-M09 (타이머 휠·배압·C10K), NET-A07 (SO_REUSEPORT·EPOLLEXCLUSIVE), LLAT-A03 (io_uring), COMM-M09 (SocketCAN), BM-M04 (UART ISR 링 버퍼), RTOS-B11 (메시지 큐)
