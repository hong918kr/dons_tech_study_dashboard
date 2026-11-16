---
id: OS-A13
time: 110분
updated: 2026-11-15
---
# OS-A13 미니 RPC 계층: 요청/응답·타임아웃·취소·스트리밍

## 학습 목표

- 바이트 스트림(Unix 소켓) 위에 **길이 prefix 프레임 + 헤더 + TLV** 를 얹고, 깨진 입력(길이 0, 16 KiB 초과, 0xFFFFFFFF, TLV 넘침)을 **몸통을 기다리기 전에** 거절하는 디코더를 짠다. 퍼즈 20000 건을 독립 참조 구현과 비교해 증명한다.
- reader 스레드 1 개 + **pending 표(req_id → slot + condvar)** 로 여러 스레드의 동시 호출을 한 연결에 다중화하고, 서버가 순서 밖으로 답해도 req_id 로 짝을 맞춘다.
- 마감(deadline)을 MONOTONIC 으로 재고 macOS 의 REALTIME 전용 `pthread_cond_timedwait` 로 바꿔 기다린다. 타임아웃이면 pending 에서 지우고, 늦게 온 응답은 버리고 센다.
- 서버 스트리밍 + CANCEL 을 만들고, "서버가 K 개 안에 멈췄다" 를 서버 쪽 카운터로 확인한다.
- 서버가 소켓을 닫거나 프로세스가 SIGKILL 당해도 **모든 대기 호출이 바로 오류로 끝나고**, 스레드는 join, fd 는 닫힌다는 것을 테스트로 보인다. gRPC·D-Bus 와 설계를 비교한다.

## 1. 문제

차량 ECU 의 진단 데몬, BMC 의 센서 서비스, 검증 장비의 호스트↔보드 제어 — 프로세스 사이(또는 MCU↔호스트) 통신은 결국 "요청 보내고 응답 받기" 다. 면접의 꼬리질문은 늘 같다: **응답이 안 오면? 순서가 바뀌면? 상대가 죽으면? 필드를 추가하면?** 이 레슨은 그 네 질문에 코드와 테스트로 답한다.

OSTEP 47.5 절(RPC)이 말하는 스텁·마샬링·런타임 라이브러리 중 **런타임** 을 직접 만든다. 전송은 OS-B14 에서 잰 AF_UNIX `SOCK_STREAM` 이다 (fd 전달은 OS-M16, 이벤트 루프는 OS-M12/OS-A11 의 몫). 스트림이라 메시지 경계가 없다 → 프레이밍이 첫 일이다.

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">프레임 = 길이 4 B + 헤더 10 B + TLV 페이로드 (모든 정수 little-endian)</text>
  <rect x="10" y="40" width="136" height="40" class="f-acc" stroke="currentColor"/>
  <text x="40" y="65" fill="currentColor">u32 len</text>
  <rect x="146" y="40" width="34" height="40" class="f-ok" stroke="currentColor"/>
  <text x="151" y="65" fill="currentColor">ver</text>
  <rect x="180" y="40" width="34" height="40" class="f-ok" stroke="currentColor"/>
  <text x="182" y="65" fill="currentColor">type</text>
  <rect x="214" y="40" width="68" height="40" class="f-ok" stroke="currentColor"/>
  <text x="222" y="65" fill="currentColor">method</text>
  <rect x="282" y="40" width="136" height="40" class="f-warn" stroke="currentColor"/>
  <text x="315" y="65" fill="currentColor">u32 req_id</text>
  <rect x="418" y="40" width="68" height="40" class="f-ok" stroke="currentColor"/>
  <text x="432" y="65" fill="currentColor">flags</text>
  <rect x="486" y="40" width="244" height="40" class="f-mut" stroke="currentColor"/>
  <text x="510" y="65" fill="currentColor">TLV · TLV · TLV …</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="10" y="96">0</text><text x="146" y="96">4</text><text x="180" y="96">5</text>
    <text x="214" y="96">6</text><text x="282" y="96">8</text><text x="418" y="96">12</text><text x="486" y="96">14</text>
  </g>
  <rect x="486" y="120" width="68" height="34" class="f-mut" stroke="currentColor"/>
  <text x="496" y="142" fill="currentColor">u16 tag</text>
  <rect x="554" y="120" width="80" height="34" class="f-mut" stroke="currentColor"/>
  <text x="564" y="142" fill="currentColor">u32 len</text>
  <rect x="634" y="120" width="96" height="34" class="f-mut" stroke="currentColor"/>
  <text x="640" y="142" fill="currentColor">value[len]</text>
  <path d="M486 80 L486 120 M730 80 L730 120" stroke="currentColor" stroke-dasharray="4 3" fill="none"/>
  <g fill="currentColor">
    <text x="10" y="140">len = 헤더 10 + 페이로드 (자기 4 B 는 제외)</text>
    <text x="10" y="162">허용 범위 [10, 16384] — 밖이면 몸통을 기다리지 않고 거절</text>
    <text x="10" y="190">type: 1 REQ · 2 RESP · 3 ERR · 4 STREAM_ITEM · 5 STREAM_END · 6 CANCEL — 모르는 type 은 버린다</text>
    <text x="10" y="212">req_id: 클라이언트가 붙인다(0 은 예약). 응답·스트림 항목·취소는 같은 req_id 로 짝을 찾는다</text>
    <text x="10" y="234">TLV: 모르는 tag 는 len 만큼 건너뛴다 → v1 이 v2 메시지를 읽을 수 있다 (하위 호환)</text>
    <text x="10" y="256">ver: 보낸 쪽이 아는 TLV 집합의 버전. 헤더 배치는 고정 (바꾸려면 새 magic/협상이 필요)</text>
  </g>
</svg>
```

`solution.c` 한 파일 안에 코덱·클라이언트·서버·테스트가 다 있다. 서버는 같은 프로세스의 스레드(socketpair) 로 돌고, 한 시험만 `fork` 한 자식 프로세스가 `/tmp/OS-A13_work/rpc.<pid>.sock` 에서 받는다 (connect 직후 unlink).

### Part A — 프레이밍과 부분 읽기/쓰기
- `tlv_next`: 모르는 tag 도 그대로 내주고(상위가 건너뜀), 잘린 머리·남은 바이트를 넘는 길이는 `-EBADMSG`. `len = 0xFFFFFFFF` 에서 u32 덧셈이 넘치지 않게.
- `frame_parse`: 완전한 프레임이면 소비한 바이트 수, 모자라면 0, 깨졌으면 `-EBADMSG`. 길이 범위는 몸통보다 먼저.
- `xp_read_full` / `xp_write_all`: `read`/`write` 가 요청보다 적게 처리하는 경우와 EINTR. 중간 EOF 는 `-ECONNRESET`, 경계 EOF 는 0.
- 검증: 나쁜 프레임 표 9 종, 퍼즈 20000 건(독립 참조 파서와 비교), 1..17 바이트 조각 재조립, **1 바이트 전송 shim**.

### Part B — 클라이언트 다중화, 타임아웃, 버전 호환
- `wait_slot`: 마감까지 condvar 대기 (가짜 깨움, macOS REALTIME 시계).
- `client_dispatch`: reader 스레드가 프레임을 주인 slot 에 배달. 모르는 type, 주인 없는 req_id(늦은 응답)는 버리고 센다.
- `client_fail_all`: 연결이 죽으면 대기 중인 모든 호출을 깨워 같은 오류로 끝낸다.
- 검증: 게이트에 붙잡힌 호출 A 가 있어도 B 가 끝난다(결정적 순서 밖 응답), 16 스레드 × 100 호출, 타임아웃 + 늦은 응답 1 건, v1↔v2 양방향, 손으로 쓴 프레임으로 돌리는 가짜 서버.

### Part C — 서버 스트리밍, 취소, 장애 주입
- `server_do_count` (서버): 항목마다 지연 + 취소 확인, 끝에 `STREAM_END{COUNT}`.
- `server_on_cancel` (서버 reader): 처리 중인 req_id 에 취소 표시 + 깨우기.
- `rpc_stream_next` / `rpc_stream_cancel` (클라이언트): 남은 항목을 먼저 내주고 끝을 알린다 / 취소 후 서버의 마지막 프레임을 기다린다.
- 검증: 50 개 스트림, 항목 하나의 마감, 100000 개짜리 스트림 3 개 받고 취소 → 서버가 3 + 64 개 안에서 멈춤, **서버가 소켓을 닫음**(대기 8 개), **서버 프로세스 SIGKILL**(대기 4 개) → 전부 `-ECONNRESET`, 마지막에 fd 수·살아 있는 스레드 수가 처음과 같다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">한 연결 위의 다중화: 쓰기는 여러 스레드(락), 읽기는 한 스레드</text>
  <rect x="10" y="40" width="120" height="120" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="20" y="62" fill="currentColor" font-weight="bold">호출 스레드들</text>
  <text x="20" y="84" fill="currentColor">rpc_call()</text>
  <text x="20" y="104" fill="currentColor">① slot 등록</text>
  <text x="20" y="124" fill="currentColor">② REQ 쓰기</text>
  <text x="20" y="144" fill="currentColor">③ slot.cv 대기</text>
  <rect x="170" y="40" width="170" height="120" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="180" y="62" fill="currentColor" font-weight="bold">pending 표 (mu)</text>
  <text x="180" y="84" fill="currentColor" font-family="monospace">id 17 → slot+cv</text>
  <text x="180" y="104" fill="currentColor" font-family="monospace">id 18 → slot+cv</text>
  <text x="180" y="124" fill="currentColor" font-family="monospace">id 19 → stream q</text>
  <text x="180" y="146" fill="currentColor">없는 id → late_drops++</text>
  <rect x="170" y="190" width="170" height="60" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="180" y="212" fill="currentColor" font-weight="bold">reader 스레드 1 개</text>
  <text x="180" y="232" fill="currentColor">EOF → fail_all</text>
  <path d="M130 100 L166 100" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M158 94 L168 100 L158 106" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M255 190 L255 164" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M249 172 L255 162 L261 172" stroke="currentColor" stroke-width="2" fill="none"/>
  <rect x="380" y="40" width="60" height="210" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="388" y="140" fill="currentColor">소켓</text>
  <path d="M130 130 C 250 130, 300 70, 376 70" stroke="currentColor" stroke-dasharray="5 3" fill="none"/>
  <text x="264" y="180" fill="currentColor" font-size="12">wmu: 프레임 통째로</text>
  <path d="M380 220 L344 220" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M352 214 L342 220 L352 226" stroke="currentColor" stroke-width="2" fill="none"/>
  <rect x="480" y="40" width="250" height="60" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="490" y="62" fill="currentColor" font-weight="bold">서버 reader</text>
  <text x="490" y="84" fill="currentColor">REQ → 큐 + active 표, CANCEL → 표시</text>
  <rect x="480" y="120" width="250" height="56" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="490" y="142" fill="currentColor" font-weight="bold">작업 큐 (qcv, signal)</text>
  <text x="490" y="162" fill="currentColor">쉬는 워커 하나만 깨운다</text>
  <rect x="480" y="196" width="250" height="56" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="490" y="218" fill="currentColor" font-weight="bold">워커 16 개 (cv: 지연·취소·종료)</text>
  <text x="490" y="238" fill="currentColor">끝나는 순서대로 RESP/ITEM 쓰기</text>
  <path d="M440 70 L476 70 M605 100 L605 116 M605 176 L605 192" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M440 230 L476 230" stroke="currentColor" stroke-dasharray="5 3" fill="none"/>
  <g fill="currentColor">
    <text x="10" y="284">규칙 1: slot 을 보내기 "전에" 등록한다 — 응답이 send() 보다 먼저 도착해도 받을 자리가 있다.</text>
    <text x="10" y="304">규칙 2: reader 는 절대 막히지 않는다 (스트림 큐가 차면 그 호출만 -ENOBUFS).</text>
    <text x="10" y="324">규칙 3: 연결이 죽었음을 아는 건 reader 뿐 → reader 가 fail_all 로 모든 대기자를 깨운다.</text>
  </g>
</svg>
```

## 2. Problem (English)

Implement the runtime of a tiny RPC system over a Unix-domain stream socket.

```c
int  tlv_next(tlv_it_t *it, tlv_t *t);                      /* 1 item, 0 end, -EBADMSG malformed */
int  frame_parse(const uint8_t *buf, size_t n, frame_t *f); /* >0 consumed, 0 need more, -EBADMSG */
int  xp_read_full(xport_t *x, void *buf, size_t n);         /* 1 ok, 0 EOF at start, -ECONNRESET mid */
int  xp_write_all(xport_t *x, const void *buf, size_t n);   /* 0 ok, -errno */
int  wait_slot(rpc_client_t *c, slot_t *s, bool want_item, int timeout_ms);  /* 0 / -ETIMEDOUT */
void client_dispatch(rpc_client_t *c, const frame_t *f);    /* reader thread → owner slot */
void client_fail_all(rpc_client_t *c, int err);             /* connection lost: fail every waiter */
int  rpc_stream_next(rpc_client_t *c, uint32_t sid, uint8_t *out, uint32_t cap, uint32_t *len, int timeout_ms);
int  rpc_stream_cancel(rpc_client_t *c, uint32_t sid, int timeout_ms);
void server_on_cancel(rpc_server_t *s, uint32_t req_id);
void server_do_count(rpc_server_t *s, const job_t *j, uint32_t n, uint32_t start, uint32_t delay_ms);
```

Wire format: `u32 len` (little-endian, bytes after the length field), a 10-byte header `{u8 version, u8 type, u16 method, u32 req_id, u16 flags}`, then TLVs `{u16 tag, u32 len, value}`. Frames are at most 16 KiB.

Constraints:

- Many threads call concurrently on one connection. Exactly one reader thread reads the socket. Writers serialize whole frames.
- A call returns `-ETIMEDOUT` after its deadline; its pending entry is removed immediately, and a response that arrives later is dropped and counted.
- Unknown frame types and unknown TLV tags are ignored (forward compatibility). A malformed frame kills the connection (`-EPROTO`) because the next frame boundary is unknown.
- If the peer closes the socket or dies, every pending call returns `-ECONNRESET` promptly, and later calls fail immediately. Nothing hangs; all threads are joined and fds closed.
- Server streaming: N `STREAM_ITEM`s then `STREAM_END{COUNT=N}`. After `CANCEL`, the server stops within K items and answers `ERR{CANCELLED}`.

Example:

```text
call ECHO{VALUE=7}            → RESP{VALUE=f(7)}
call ECHO{VALUE=5, DELAY=400} timeout 50 ms → -ETIMEDOUT   (later RESP → late_drops = 1)
open COUNT{COUNT=100000, DELAY=5} → ITEM 0, ITEM 1, ITEM 2, (CANCEL) → ERR{CANCELLED}
server killed while 4 calls wait  → all four return -ECONNRESET
```

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o st && ./st
```

`/* BUILD: -pthread */` 가 첫 줄에 있다 (macOS 는 `-pthread` 없이도 링크되지만 Linux 이식을 위해 붙인다). TODO 는 11 개:

| Part | TODO | 쓰는 쪽 |
|---|---|---|
| A | `tlv_next`, `frame_parse`, `xp_read_full`, `xp_write_all` | 양쪽 |
| B | `wait_slot`, `client_dispatch`, `client_fail_all` | 클라이언트 |
| C | `rpc_stream_next`, `rpc_stream_cancel` / `server_on_cancel`, `server_do_count` | 클라이언트 / 서버 |

`rpc_call`, `rpc_stream_open`, reader 루프, 서버 reader/워커, `server_sleep`(취소·종료로 깨는 지연) 은 주어진다. 시작 상태의 실제 실행:

```text
--- Part A: 코덱 ---
FAIL  r == (int)n  (line 777)
...
FAIL  xp_read_frame(&rx, buf, sizeof buf, &f) == -EBADMSG  (line 902)
SKIP  Part B, C (Part A 코덱/전송이 먼저 통과해야 한다)
2/17 passed
```

종료 코드 1, 0.13 초. Part A 가 통과해야 B·C 가 돈다 (코덱 없이는 서버와 말을 못 한다). 모든 기다림에는 마감이 있고, 하네스 전체에 60 초 `alarm` 감시 타이머가 있다. 실제로 Part A 만 채운 상태로 돌려 보면 32/66, A+B 를 채우면 50/66 이 나오고 각각 12 초, 20 초 안에 끝난다 (Part B/C 의 마감들을 다 쓰고 끝남. 멈추지 않는다).

추천 순서: `tlv_next` → `frame_parse` → `xp_read_full` / `xp_write_all` (Part A 통과) → `wait_slot` → `client_dispatch` → `client_fail_all` → 서버 쪽 `server_do_count`, `server_on_cancel` → `rpc_stream_next`, `rpc_stream_cancel`.

## 4. 힌트

<details><summary>힌트 1 — tlv_next 의 넘침 없는 비교</summary>

`it->off + 6 + len <= it->n` 은 `len = 0xFFFFFFFF` 에서 u32 가 한 바퀴 돌아 참이 될 수 있다. 먼저 `rem = n - off` (off ≤ n 이 불변식이라 안전), `rem < 6` 이면 오류, 그다음 `len > rem - 6` 이면 오류. 뺄셈만 쓰고 덧셈은 검사 뒤에.
</details>

<details><summary>힌트 2 — frame_parse 의 검사 순서</summary>

① 4 바이트 미만 → 0 ② 길이 범위 → 오류 ③ 몸통 미완 → 0 ④ version 0 → 오류 ⑤ TLV 전체 한 번 걸어 보기. ②가 ③보다 먼저여야 "길이 0xFFFFFFFF 라고 해 놓고 아무것도 안 보내는" 상대에게 4 GB 를 기다리지 않는다. 모르는 type 은 여기서 거절하지 않는다.
</details>

<details><summary>힌트 3 — xp_read_full 의 세 가지 끝</summary>

`read` 가 0 을 돌려주면 EOF 다. 그때까지 읽은 게 0 바이트면 "프레임 경계에서 깔끔하게 끝" (0), 아니면 "프레임 중간에 끊김" (-ECONNRESET). -1 이면 `errno == EINTR` 만 다시 시도. 1 바이트 shim 은 `n` 을 1 로 잘라서 이 루프가 실제로 돌게 만든다.
</details>

<details><summary>힌트 4 — wait_slot 과 macOS 시계</summary>

macOS 에는 `pthread_condattr_setclock` 이 없다 (SDK 26.4 `pthread.h` 에 선언 자체가 없음). `pthread_cond_timedwait` 의 절대 시각은 REALTIME 이다. 마감은 `now_ns()`(MONOTONIC) 로 한 번 정하고, 루프마다 `rt_after(deadline - now)` 로 절대 시각을 다시 만든다. 벽시계가 앞뒤로 뛰어도 최대 한 번의 대기만 틀린다. 조건은 `while`.
</details>

<details><summary>힌트 5 — client_dispatch 에서 "늦은 응답" 판단</summary>

타임아웃 난 호출은 `rpc_call` 이 mu 안에서 slot 을 지웠다. reader 도 같은 mu 안에서 찾으니 "찾았는데 그 사이 지워짐" 은 없다. 못 찾으면(또는 이미 done) 그게 늦은 응답이다. 복사 대상 `s->resp` 는 호출자의 스택 버퍼다 — slot 이 살아 있을 때만 유효하다는 것이 이 락의 진짜 이유다.
</details>

<details><summary>힌트 6 — 취소 경주</summary>

CANCEL 이 서버에 닿기 전에 서버가 항목을 몇 개 더 보낼 수 있다. 그래서 `cancel_sent` 를 **보내기 전에** 세우고 그 뒤 도착한 항목은 `after_cancel++` 로 버린다. 서버 쪽은 `server_sleep` 이 취소 표시를 보자마자 깬다 (지연 0 이어도 먼저 확인). 스트림이 이미 끝난 뒤의 CANCEL 은 서버에서 `cancel_unknown++` 일 뿐 정상이다.
</details>

## 5. 풀이

### Part A — 프레이밍: 무엇을 믿고 무엇을 안 믿나

스트림 소켓은 `write` 두 번이 `read` 한 번에 붙어서 오거나, 한 번이 세 조각으로 오는 것을 막지 않는다. 메시지 경계를 다시 세우는 방법은 세 가지다.

| 방식 | 경계 | 깨진 바이트 후 | 쓰는 곳 |
|---|---|---|---|
| 길이 prefix | 앞의 길이 필드 | 경계를 잃는다 → 연결을 끊고 다시 | gRPC(5 B prefix), D-Bus, 이 레슨 |
| 구분자 + 바이트 채우기 | 0x00 (COBS) / 0xC0 (SLIP) | 다음 구분자에서 다시 맞춘다 | UART 링크 (COMM-B11, COMM-B12) |
| 고정 크기 | 슬롯 크기 | 다음 슬롯 | 공유 메모리 링 (OS-A12, DS-A01) |

소켓은 바이트를 잃거나 바꾸지 않으니 길이 prefix 가 가장 싸다. 대신 **길이 필드가 깨지면 그 뒤 전부가 쓰레기** 다 — 그래서 디코더는 깨진 프레임을 받으면 "이 프레임만 버리기" 가 아니라 연결을 끊는다 (`client_reader` 의 `-EPROTO`). UART 처럼 바이트가 깨질 수 있는 링크는 연습 1 의 COBS + CRC 로 간다.

디코더의 핵심 세 줄:

```c
if (len < RPC_HDR_LEN || len > RPC_MAX_FRAME) return -EBADMSG;   /* 몸통보다 먼저 */
if (n - RPC_LEN_LEN < len) return 0;                             /* 모자람 */
if (len > rem - 6) return -EBADMSG;                              /* TLV: 더하지 말고 남은 양과 비교 */
```

**모르는 것은 버리고 깨진 것은 거절한다.** 모르는 type(99) 과 모르는 tag(777) 은 오류가 아니다 — 길이를 아니까 건너뛸 수 있고, 그래야 v1 이 v2 를 읽는다. HTTP/2 도 모르는 프레임 type 은 무시하라고 정한다 (RFC 9113 §4.1, 기억 기반 — 원문 대조 안 함). 반대로 길이가 틀린 것은 건너뛸 방법이 없다.

퍼즈의 실제 출력 (고정 시드, 20000 건: 비트 뒤집기 / 자르기 / TLV 길이 덮어쓰기 / 프레임 길이 덮어쓰기 각 1/4):

```text
      fuzz: accepted=3819 need_more=6899 rejected=9282 mismatch=0
```

`mismatch` 는 (a) 바이트를 하나씩 따라가며 `uint64_t` 로 계산하는 **독립 참조 파서** 와 판정이 다르거나, (b) 받아들인 프레임의 payload/TLV 가 버퍼 밖을 가리키는 경우를 센다. ASan+UBSan 빌드에서도 0 이다. "몰래 잘못 받아들이는" 경우와 "멀쩡한 걸 거절하는" 경우를 둘 다 잡는 것이 참조 비교의 장점이다.

**부분 읽기는 테스트로 강제한다.** 로컬 소켓은 작은 프레임을 거의 항상 한 번에 준다 — 그래서 `read` 를 한 번만 부르는 버그가 몇 달 숨어 있다가 부하나 큰 메시지에서 터진다. `xport_t.max_chunk = 1` shim 은 매 `read`/`write` 를 1 바이트로 자른다. 실제 결과: 5 프레임 `total` 바이트에 대해 `writes == total`, `reads == total` (호출 수 == 바이트 수). Part B 에서는 클라이언트·서버 둘 다 shim 위에서 4 스레드 × 20 호출이 통과한다.

### Part B — pending 표와 마감

`rpc_call` 의 순서가 설계의 전부다:

1. mu 안에서 `slot_alloc` — req_id 발급(0 과 쓰는 중인 id 건너뜀), 호출자 버퍼 주소 등록.
2. mu 밖에서 `send_frame` — 프레임 하나를 `wmu` 안에서 통째로 (`xp_write_all`). 여러 스레드의 프레임이 섞이면 상대 디코더가 경계를 잃는다.
3. mu 안에서 `wait_slot` → 결과를 읽고 `slot_release`. **타임아웃이어도 여기서 지운다.**

1 과 2 의 순서를 바꾸면 (보내고 나서 등록) 서버가 아주 빠를 때 응답이 먼저 도착해 "주인 없음" 으로 버려지고 호출은 타임아웃까지 멈춘다. 드물어서 더 나쁜 버그다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">시간축: 타임아웃 뒤의 늦은 응답 (위) / 스트림 취소 (아래)</text>
  <line x1="120" y1="60" x2="720" y2="60" stroke="currentColor"/>
  <line x1="120" y1="110" x2="720" y2="110" stroke="currentColor"/>
  <text x="10" y="64" fill="currentColor">클라이언트</text>
  <text x="10" y="114" fill="currentColor">서버</text>
  <path d="M140 60 L180 110" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="130" y="50" fill="currentColor" font-size="12">REQ 17 (지연 400)</text>
  <rect x="140" y="52" width="60" height="16" class="f-acc" stroke="currentColor"/>
  <text x="206" y="50" fill="currentColor" font-size="12">50 ms: -ETIMEDOUT, slot 삭제</text>
  <rect x="180" y="102" width="400" height="16" class="f-mut" stroke="currentColor"/>
  <text x="300" y="132" fill="currentColor" font-size="12">server_sleep(400)</text>
  <path d="M580 110 L620 60" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="440" y="36" fill="currentColor" font-size="12">늦은 RESP 17 → 주인 없음 → late_drops++</text>
  <line x1="120" y1="190" x2="720" y2="190" stroke="currentColor"/>
  <line x1="120" y1="250" x2="720" y2="250" stroke="currentColor"/>
  <text x="10" y="194" fill="currentColor">클라이언트</text>
  <text x="10" y="254" fill="currentColor">서버</text>
  <path d="M200 250 L230 190 M260 250 L290 190 M320 250 L350 190" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="200" y="270" fill="currentColor" font-size="12">ITEM 0, 1, 2 (5 ms 간격)</text>
  <path d="M360 190 L400 250" stroke="currentColor" stroke-width="2" class="f-warn" fill="none"/>
  <text x="340" y="180" fill="currentColor" font-size="12">cancel_sent=1 → CANCEL</text>
  <path d="M380 250 L410 190" stroke="currentColor" stroke-dasharray="4 3" fill="none"/>
  <text x="415" y="206" fill="currentColor" font-size="12">경주로 온 ITEM → after_cancel++</text>
  <path d="M400 250 L470 190" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="480" y="236" fill="currentColor" font-size="12">server_sleep 즉시 깸 → ERR CANCELLED</text>
  <rect x="470" y="182" width="120" height="16" class="f-ok" stroke="currentColor"/>
  <text x="600" y="180" fill="currentColor" font-size="12">cancel() 반환 0</text>
  <text x="10" y="294" fill="currentColor">서버가 보낸 항목 수 last_stream_items 가 3 + K 이하인지 서버 쪽 카운터로 확인한다 (K = 64).</text>
</svg>
```

**대기 루프.** macOS 는 condvar 시계를 고를 수 없다. 그래서:

```c
uint64_t deadline = now_ns() + timeout_ms * 1e6;         /* MONOTONIC: 한 번만 */
while (!s->done && !(want_item && s->qn > 0)) {
    uint64_t now = now_ns();
    if (now >= deadline) return -ETIMEDOUT;
    struct timespec ts = rt_after(deadline - now);       /* 남은 시간을 REALTIME 절대 시각으로 */
    pthread_cond_timedwait(&s->cv, &c->mu, &ts);
}
```

벽시계(NTP 보정, 사용자 변경)가 1 시간 뒤로 가도 한 번의 `timedwait` 가 길어질 뿐, 다음 루프에서 MONOTONIC 마감으로 바로잡힌다. Linux 라면 `pthread_condattr_setclock(CLOCK_MONOTONIC)` 이 정석이고, Darwin 에는 비표준 `pthread_cond_timedwait_relative_np` 도 있다 (SDK 헤더 558 행에서 확인, 여기선 안 씀).

**slot 마다 condvar.** 전역 condvar 하나에 broadcast 하면 응답 하나마다 기다리는 호출 스레드 전부가 깨어 자기 것인지 확인한다 (thundering herd). slot 마다 cv 를 두면 `finish()` 의 `signal` 이 정확히 주인 하나만 깨운다. 서버에서도 같은 일이 있었다 — 아래 성능 절.

**결정적 "순서 밖 응답" 테스트.** "느린 요청 뒤 빠른 요청" 을 시간으로 만들면 부하 높은 기계에서 흔들린다. 대신 시험 훅 메서드 `M_GATE` 는 게이트가 열릴 때까지 서버 워커를 붙잡는다. 하네스는 ① A(GATE) 를 다른 스레드로 호출 ② 서버 `gate_waiting == 1` 이 될 때까지 폴링 ③ B(ECHO) 를 호출해 성공 ④ **A 가 아직 안 끝났음** 을 확인 ⑤ 게이트를 열고 A 성공. 시간에 기대는 곳이 없다. 16 스레드 × 100 호출(서버 지연 0~2 ms 무작위) 실행의 관찰:

```text
      동시 호출 최대 pending=16, 서버가 순서 밖으로 끝낸 횟수=532
```

1600 호출 중 500 번 넘게 받은 순서와 다르게 끝났고 1600 개 결과가 전부 자기 요청의 `echo_fn(v)` 와 맞았다.

**타임아웃과 늦은 응답.** 서버 지연 400 ms, 마감 50 ms → `-ETIMEDOUT`, 바로 `n_pending == 0`, 3 초 안에 `late_drops` 가 정확히 1 증가, 다음 호출은 정상. 서버의 지연은 최소 400 ms 가 보장되므로 이 판정은 기계 부하와 무관하다.

**버전 호환 행렬.** 헤더 배치는 고정이고 확장은 TLV 로만 한다. 규칙은 protobuf 와 같다: 번호는 재사용하지 않는다, 새 필드는 없어도 되는 것(기본값)으로 정의한다, 모르는 것은 건너뛴다.

| 클라이언트 → 서버 | 결과 (실제 검사) |
|---|---|
| v2 → v2 | `SERVER_INFO="mini-rpc/2"`, `TRACE_ID` 되돌아옴 |
| v1 → v2 | 값 정상, 클라이언트가 모르는 tag 1 개(`SERVER_INFO`) 건너뜀, `peer_version == 2` |
| v2 → v1 | 값 정상, 서버 `unknown_tlv == 1` (`TRACE_ID` 건너뜀), 응답에 `SERVER_INFO` 없음 → 기본값 |

**가짜 서버.** 실제 서버 대신 테스트가 socketpair 반대편에서 프레임을 손으로 쓴다: 모르는 type 99 → `unknown_type == 1`, 아무도 안 기다리는 id → `late_drops == 1`, 앞에 모르는 tag 가 붙은 RESP → 호출 성공(`unknown == 1`), 마지막으로 길이 3 짜리 깨진 프레임 → reader 가 `-EPROTO` 로 fail_all, 이후 호출은 즉시 `-EPROTO`. 실서버로는 만들기 힘든 입력을 결정적으로 만든다.

### Part C — 스트리밍, 취소, "멈추지 않는다" 의 증명

서버 스트림은 항목마다 `server_sleep(delay)` 를 부르고 반환값으로 갈린다: 0 → ITEM 전송, 1(취소) → `ERR{CANCELLED}` + `stopped_by_cancel++`, 2(연결 종료) → 조용히 끝. `server_sleep` 은 취소·종료 표시를 `cv` 에서 기다리므로 **지연 중에도 바로 깬다** — "5 ms 자고 확인" 이 아니다. 실제 결과:

```text
      취소: 서버가 보낸 항목=3 (받은 3 + 취소 뒤 도착 0), 서버 stopped_by_cancel=1
```

검사는 `3 ≤ 보낸 수 ≤ 3 + 64` 와 `after_cancel ≤ 64` (K = 64) 다. 측정값은 매번 3 이었지만 경계는 부하를 견디게 넉넉히 잡았다. 클라이언트 쪽 `rpc_stream_next` 는 done 이어도 **큐에 남은 항목을 먼저** 내준다. 반대로 하면 서버가 50 개를 보내고 바로 END 를 보냈을 때 마지막 몇 개가 사라진다.

**스트림 큐가 차면?** reader 는 한 스트림 때문에 멈추면 안 된다 (다른 호출의 응답까지 막힘 = head-of-line blocking). 이 레슨은 `STREAM_QMAX = 256` 을 넘으면 그 스트림만 `-ENOBUFS` 로 끝낸다. 진짜 해법은 흐름 제어(window credit, HTTP/2 WINDOW_UPDATE) 이고 연습 3 에서 시뮬레이션한다.

**장애 주입 1 — 서버가 소켓을 닫는다.** 10 초 걸릴 호출 8 개(마감 5 초)가 서버에 접수된 것을 `reqs` 카운터로 확인한 뒤 `shutdown(server_fd, SHUT_RDWR)`. 클라이언트 reader 의 `read` 가 0 → `client_fail_all(-ECONNRESET)` → 8 개 slot 이 모두 깨어난다. 실측: `대기 호출 8 개가 끝나기까지 0.3~4.5 ms`. 검사는 시간이 아니라 **반환값이 `-ETIMEDOUT` 이 아니라 `-ECONNRESET`** 인지다 — 그게 "마감보다 먼저 끝났다" 의 결정적 증거다. 서버 워커들은 reader 가 세운 `closing` 으로 깨어 응답 없이 끝나고 join 된다.

**장애 주입 2 — 서버 프로세스 SIGKILL.** 이름 있는 소켓을 쓰는 유일한 시험이다. 부모가 `bind` + `listen` 한 뒤 `fork` → 자식이 `accept` 해서 서버를 돌린다. 부모는 `connect` 직후 경로를 `unlink` 하고 `/tmp/OS-A13_work` 도 지운다 (연결된 소켓은 이름 없이도 산다 — `stat` 이 `ENOENT` 인지 검사). 대기 호출 4 개가 나간 뒤 `kill(pid, SIGKILL)` → `waitpid` 가 `WTERMSIG == SIGKILL` → 4 개 모두 `-ECONNRESET`, 다음 호출은 보내지도 않고 즉시 `-ECONNRESET`. 커널이 죽은 프로세스의 fd 를 닫아 주므로 상대는 EOF(또는 ECONNRESET) 를 받는다 — **같은 기계 안의 IPC 에서는 "상대가 죽었다" 를 커널이 알려 준다.** TCP 너머라면 아무 신호도 안 올 수 있다 (케이블 뽑힘) → keepalive/heartbeat 가 필요하다 (Q6).

쓰기 쪽도 막아 둔다: 상대가 없는 소켓에 `write` 하면 기본 동작은 SIGPIPE 로 프로세스가 죽는 것이다. macOS 는 `setsockopt(SO_NOSIGPIPE)` 로 그 대신 `EPIPE` 를 받는다 (`man 2 setsockopt`: "prevents SIGPIPE from being raised when a write fails on a socket to which there is no reader"). Linux 는 `send(..., MSG_NOSIGNAL)` 이 흔한 방법이다 (macOS SDK 26.4 `sys/socket.h` 에도 `MSG_NOSIGNAL 0x80000` 이 있지만 `man 2 send` 에는 없다). 쓰기가 실패하면 `client_write_failed` 가 `shutdown` + fail_all 한다 — 프레임이 반만 나갔을 수 있어 그 연결은 더 쓸 수 없다.

**누수 없음.** 하네스 마지막 두 검사: `count_fds() == fds0` (0..255 fd 를 `fcntl(F_GETFD)` 로 셈), `g_threads_live == 0` (`spawn()` 이 만든 스레드가 끝날 때 빼는 원자 카운터, join 뒤 확인). 클라이언트 해제는 `shutdown` → reader 가 EOF 로 깸 → join → close 순서다. `close` 를 먼저 하면 reader 가 막혀 있는 fd 번호가 다른 곳에서 재사용될 수 있다.

### Part C — 실제 시스템과 나란히 놓기

이 레슨의 각 장치가 실제 시스템에서 어떤 이름으로 있는지. gRPC·D-Bus·Binder 칸은 공개 문서에 대한 기억 기반이며 이 레슨에서 실행해 확인하지 않았다 (Binder 의 커널 쪽은 OS-A14 에서 Mach 와 함께 다룬다).

| 장치 | 이 레슨 | gRPC (HTTP/2) | D-Bus | Android Binder |
|---|---|---|---|---|
| 메시지 경계 | u32 길이 prefix | 5 B prefix (압축 플래그 + 길이) on DATA 프레임 | 헤더에 몸통 길이 | 커널이 트랜잭션 단위로 복사 |
| 요청↔응답 짝 | req_id | stream id | serial / REPLY_SERIAL | 커널이 호출 스레드로 reply 를 되돌림 (id 불필요) |
| 마감 | 클라이언트만 | `grpc-timeout` 로 서버까지 전파 | 클라이언트 라이브러리 | 동기 호출, 마감 개념 약함 |
| 취소 | CANCEL 프레임 | RST_STREAM | 없음 (응답 무시) | 없음 (oneway 는 응답 자체가 없음) |
| 스트리밍 | 서버 → 클라이언트 | 양방향 | SIGNAL 브로드캐스트 | 콜백 객체로 흉내 |
| 필드 추가 | TLV 건너뛰기 | protobuf unknown field | 시그니처가 바뀌면 깨짐 → 새 메서드/인터페이스 | AIDL 인터페이스 버전 |
| 상대 죽음 | EOF → fail_all | keepalive PING, GOAWAY | 버스가 NameOwnerChanged 알림 | death notification (linkToDeath) |

공통 교훈: **짝짓기·마감·취소·죽음 감지** 네 가지는 어떤 RPC 든 어딘가에서 반드시 푼다. 커널이 풀어 주면(Binder) 사용자 코드가 단순해지고, 라이브러리가 풀면(gRPC) 이식성이 생기고, 버스가 풀면(D-Bus) 발견·권한이 따라온다.

### 성능 — 다중화의 값 (`demo_bench.c`)

<!-- code: demo_bench.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_bench.c -o demo_bench && ./demo_bench
```

`demo_bench.c` 는 같은 폴더의 `solution.c` 를 `#include` 하고(`OS_A13_NO_MAIN` 으로 테스트 main 제외) ECHO(지연 0) 를 닫힌 루프로 1 초 돌린다(앞 200 ms 버림). 기준선은 같은 크기 프레임을 socketpair 위에서 바로 주고받는 핑퐁이다 (reader 스레드·pending 표·워커 풀 없음).

측정 환경: Apple M6, Apple clang 21.0.0 (clang-2100.0.123.102), 다른 작업과 공유 중. `uptime` load averages 측정 직후 `27.45 19.85 13.87` (1회), `26.97 20.00 14.00` (2회). **절대값은 믿지 말고 비율만 본다.**

| 서버 큐 깨우기 | 호출자 | calls/s (1회) | p50 us | p99 us | calls/s (2회) | p50 us | p99 us |
|---|---|---|---|---|---|---|---|
| raw 핑퐁 (기준) | 1 | 43407 | 5 | 269 | 57757 | 5 | 166 |
| 큐 전용 `qcv` + `signal` (solution) | 1 | 15528 | 23 | 822 | 33641 | 23 | 112 |
| 〃 | 4 | 33740 | 62 | 1102 | 64418 | 50 | 261 |
| 〃 | 16 | 42075 | 269 | 2586 | 66473 | 207 | 758 |
| 공유 `cv` + `broadcast` (실험) | 1 | 10055 | 63 | 737 | 11357 | 62 | 486 |
| 〃 | 4 | 21922 | 117 | 1241 | 22641 | 117 | 1138 |
| 〃 | 16 | 51440 | 247 | 1256 | 29244 | 321 | 4839 |

읽는 법:

- **RPC 1 호출의 RTT ≈ raw 의 4~5 배** (p50 23 vs 5 µs). 요청 하나가 스레드 경계를 네 번 건넌다: 호출 스레드 → (소켓) → 서버 reader → (condvar) → 워커 → (소켓) → 클라이언트 reader → (condvar) → 호출 스레드. raw 핑퐁은 두 번이다. 문맥 전환 비용은 OS-M02 에서 잰 그대로 여기에 더해진다.
- **동시 호출자가 늘면 처리량이 오른다** (1 → 16 에서 약 2~3 배). 한 연결 위에서 여러 요청이 겹쳐 날아가는 것이 다중화의 값이다. 대신 p50 이 늘어난다 (대기열).
- **실험: 공유 cv 에 broadcast.** 처음 버전은 서버의 작업 큐·취소·게이트가 `cv` 하나를 같이 썼다. 이 경우 `signal` 은 틀린다 — 지연 중인 워커(`server_sleep`)가 깨어나 자기 일이 아니라며 다시 자면 쉬는 워커는 못 깨어나 일감이 큐에 남는다. 그래서 broadcast 를 썼고, 요청 하나마다 워커 16 개가 다 깼다. 큐 전용 `qcv` 를 따로 두고 `signal` 로 바꾸니 1 호출 p50 이 약 62 → 23 µs 로 줄었다 (두 번 다 같은 방향). 16 호출자에선 차이가 잡음 안이다 (워커가 대부분 바빠서 깨울 쉬는 워커가 적다). 실험판은 `server_worker` 의 대기를 `cv` 로, `server_reader` 의 `signal(&s->qcv)` 를 `broadcast(&s->cv)` 로 되돌린 4 줄 차이이고 파일로는 두지 않았다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so          # 66/66 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o so_as && ./so_as
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o so_ts && ./so_ts
```

실제 결과: 일반 빌드 30 회 연속 + 다른 cwd(`/`) 에서 `66/66 passed` (한 번에 약 0.8 초), ASan+UBSan 2 회 `66/66 passed` (stderr 0 줄), TSan 단독 실행 약 240 회(순차 215 + 4 개 동시 24) 모두 `66/66 passed`, stderr 0 줄. 단, 초기에 TSan 한 번이 stderr 74 줄을 냈는데 내용을 저장하지 못했고 그 뒤 재현되지 않았다 — 드문 경주가 남아 있을 가능성을 완전히 배제하지는 못한다. 실행 뒤 `/tmp/OS-A13_work` 는 남지 않는다. SIGKILL 당한 자식은 시그널로 죽은 것이라 크래시 리포트(.ips) 대상이 아니다 (macOS 는 SIGKILL 로 죽은 프로세스의 리포트를 만들지 않는다고 알려져 있음 — 이번 실행 뒤 `~/Library/Logs/DiagnosticReports` 를 확인하지는 않았다).

작성 중 실제로 한 번 틀렸던 곳: 장애 주입 1 에서 `base = reqs` 를 **호출 스레드를 띄운 뒤** 읽어, 그 사이 접수된 요청이 base 에 섞여 `reqs == base + 8` 이 영영 안 됐다 (5 회 중 5 회 FAIL). 카운터 기준값은 일을 시작하기 전에 읽어야 한다.

## 7. 자주 하는 실수

1. **보내고 나서 pending 에 등록** — 빠른 응답이 "주인 없음" 으로 버려지고 호출은 마감까지 멈춘다. → 등록 → 보내기 → 기다리기.
2. **타임아웃 뒤 slot 을 안 지우거나 락 밖에서 지움** — reader 가 이미 반환한 호출자의 스택 버퍼(`s->resp`)에 쓴다 (스택 UAF). → 지우기와 배달을 같은 mu 안에서.
3. **프레임을 여러 `write` 로 나눠 쓰면서 락을 프레임 단위로 안 잡음** — 두 스레드의 헤더와 몸통이 섞인다. → `wmu` 안에서 `xp_write_all` 한 번.
4. **`read` 한 번이면 프레임 하나라고 가정** — 로컬 테스트는 통과, 부하·큰 메시지에서 깨진다. → `xp_read_full` + 1 바이트 shim 테스트.
5. **길이를 믿고 먼저 할당/대기** — `len = 0xFFFFFFFF` 한 줄로 메모리 고갈 또는 영원한 대기. → 범위 검사가 몸통 읽기보다 먼저.
6. **`off + 6 + len <= n`** — u32 넘침으로 검사 통과. → `len > rem - 6`.
7. **모르는 tag/type 을 오류로 처리** — 서버를 먼저 올리면 옛 클라이언트가 전부 죽는다. → 건너뛰고 센다. 깨진 길이만 오류.
8. **연결이 죽었을 때 대기자를 안 깨움** — 모든 호출이 각자 마감까지 멈춘다 (마감이 없으면 영원히). → reader 가 `fail_all`, 이후 호출은 `dead` 를 보고 즉시 실패.
9. **SIGPIPE 를 안 막음** — 상대가 죽은 뒤 첫 `write` 에서 프로세스 전체가 조용히 죽는다. → `SO_NOSIGPIPE`(macOS) / `MSG_NOSIGNAL`(Linux).
10. **`pthread_cond_timedwait` 에 MONOTONIC 시각을 넘김 (macOS)** — REALTIME 으로 해석되어 즉시 타임아웃 또는 수십 년 대기. → `rt_after(남은 시간)`.
11. **스트림 END 를 받자마자 남은 항목을 버림** — 마지막 항목들 유실. → 큐 먼저, 끝은 나중.
12. **errno 숫자를 선로에 실음** — `ECANCELED` 는 macOS 89, Linux 125 (각 헤더에서 확인). → 프로토콜 고유 상태 코드(여기선 gRPC 번호: CANCELLED 1, RESOURCE_EXHAUSTED 8, UNIMPLEMENTED 12).

## 8. 면접 Q&A

<details><summary>Q1. 요청과 응답을 어떻게 짝짓나요? 응답이 순서대로 온다고 가정하면 안 되나요?</summary>

요청마다 연결 안에서 유일한 req_id 를 붙이고, 응답이 그 id 를 그대로 싣는다. 클라이언트는 id → slot 표를 둔다. 순서 가정(FIFO 매칭)은 서버가 요청을 하나씩 순서대로 처리할 때만 맞고, 그러면 느린 요청 하나가 뒤의 모든 요청을 막는다 (head-of-line blocking). HTTP/1.1 파이프라이닝이 이 문제로 실패했고 HTTP/2 가 스트림 id 로 푼 것이 바로 이 구조다. **꼬리: id 가 32 비트를 다 돌면?** 0 과 쓰는 중인 id 를 건너뛰며 재사용한다 (연습 6). 늦은 응답이 재사용된 id 의 새 호출에 붙는 위험은 "id 재사용 간격 ≫ 최대 마감" 이면 사실상 없다. 32 비트에 초당 10 만 호출이면 한 바퀴에 약 12 시간이다.
</details>

<details><summary>Q2. 타임아웃 난 호출의 응답이 나중에 오면 어떻게 되나요? 서버는 그 일을 계속하나요?</summary>

클라이언트는 마감 순간 pending 에서 지우고, 나중에 온 응답은 주인이 없으니 버리고 센다 (`late_drops`). 이 레슨의 서버는 그 일을 끝까지 한다 — 낭비다. 개선 두 가지: ① 타임아웃 때 CANCEL 을 보낸다 ② **마감 자체를 요청에 싣는다** (gRPC 의 `grpc-timeout` 헤더) → 서버가 남은 시간이 없으면 시작도 안 하고, 하위 호출에는 남은 예산만 넘긴다 (연습 5). **꼬리: 타임아웃이 났으면 서버가 그 일을 안 한 건가?** 모른다. 그게 RPC 의 본질적 문제다 — 응답이 없다는 것은 "실행 안 됨" 과 "실행됐지만 응답 유실" 을 구별하지 못한다. 그래서 재시도는 멱등 연산에만, 아니면 중복 제거 표 (연습 4).
</details>

<details><summary>Q3. 상대 프로세스가 죽은 것을 어떻게 아나요? 왜 모든 호출이 바로 끝나죠?</summary>

같은 기계의 Unix 소켓이면 커널이 죽은 프로세스의 fd 를 닫고, 우리 쪽 `read` 가 0(EOF) 또는 ECONNRESET 을 돌려준다. 그걸 아는 것은 소켓을 읽는 reader 스레드 하나뿐이므로 reader 가 `fail_all` 로 기다리는 slot 전부를 같은 오류로 끝내고 깨운다. 이후 호출은 `dead` 를 보고 보내지도 않는다. 테스트에서 SIGKILL 뒤 4 개 호출이 마감(5 초) 대신 `-ECONNRESET` 으로 끝났다. **꼬리: TCP 원격이라면?** 상대 호스트 전원이 나가면 FIN 도 RST 도 안 온다. 그래서 TCP keepalive 또는 애플리케이션 heartbeat(gRPC 는 HTTP/2 PING 기반 keepalive) 가 필요하다. 공유 메모리라면 소켓 같은 신호가 없어 heartbeat + `kill(pid, 0)` 을 쓴다 (OS-A12).
</details>

<details><summary>Q4. 필드를 추가해도 옛 클라이언트가 안 깨지게 하려면? 이 설계가 틀리는 경우는?</summary>

규칙: tag 번호를 재사용하지 않는다, 새 필드는 "없으면 기본값" 으로 의미가 통해야 한다, 디코더는 모르는 tag 를 건너뛴다. v1↔v2 양방향 테스트로 확인했다. **틀리는 경우**: ① 새 필드가 의미상 필수인 경우 (예: 새 인증 토큰 없으면 거절해야 함) — 건너뛰기가 보안 구멍이 된다. 이럴 땐 "critical" 비트를 tag 에 두고 모르는 critical 필드면 거절한다 (TLS 확장 일부·CBOR 등의 아이디어). ② 기존 필드의 의미·단위를 바꾼 경우 (ms → us) — 형식상 호환이라 테스트로 안 잡힌다. 새 tag 를 만들어야 한다. ③ 헤더 배치 변경 — TLV 로 못 푼다, 새 magic/버전 협상 필요. ④ 옛 노드가 모르는 필드를 **버리고 다시 직렬화** 해 전달하면 새 필드가 중간에서 사라진다 → protobuf 가 unknown field 를 보존하는 이유 (proto3 도 3.5 부터 보존 — 기억 기반, 확인 필요).
</details>

<details><summary>Q5. gRPC 와 비교해 보세요. 무엇이 같고 무엇이 없나요?</summary>

같은 것: 길이 prefix 메시지(gRPC 는 1 B 압축 플래그 + 4 B 길이, big-endian), 요청별 id(HTTP/2 stream id — 클라이언트 시작은 홀수), 서버 스트리밍, 취소(HTTP/2 RST_STREAM), 모르는 필드 건너뛰기(protobuf). 없는 것: ① **흐름 제어** — HTTP/2 는 스트림별·연결별 window 와 WINDOW_UPDATE 로 느린 소비자를 보호한다 (우리는 큐가 차면 -ENOBUFS). ② **마감 전파** (`grpc-timeout`) 와 취소 전파 — 서버가 하위 호출에 남은 마감을 넘긴다. ③ 헤더 압축(HPACK), 메타데이터, 상태 코드 + 메시지 체계. ④ 연결 관리: keepalive PING, GOAWAY 로 우아한 종료, 재연결·로드밸런싱. ⑤ 클라이언트 스트리밍·양방향 스트리밍. gRPC 세부(헤더 이름, RST_STREAM 오류 코드)는 문서 기억 기반이다 — 여기서 대조하지 않았다.
</details>

<details><summary>Q6. D-Bus 는 어떻게 하나요? 언제 D-Bus 대신 이런 직접 RPC 를 쓰나요?</summary>

D-Bus(명세 기억 기반, 여기서 검증 안 함): 메시지 헤더에 송신자가 붙이는 32 비트 serial 이 있고, METHOD_RETURN/ERROR 메시지는 헤더 필드 REPLY_SERIAL 로 원래 요청을 가리킨다 — 우리의 req_id 와 같은 역할이다. 메시지 종류는 METHOD_CALL/METHOD_RETURN/ERROR/SIGNAL, 응답 불필요 플래그(NO_REPLY_EXPECTED) 가 있다. 타임아웃은 클라이언트 라이브러리 쪽에서 건다. 차이: 중앙 **버스 데몬** 이 이름(well-known name)을 관리하고 메시지를 중계한다 → 발견·권한·브로드캐스트(SIGNAL)를 얻는 대신 홉 하나와 데몬 의존이 생긴다. 타입 시스템(시그니처 문자열)이 있어 TLV 보다 엄격하다. **꼬리: 언제 직접 RPC?** 두 프로세스가 고정 쌍이고 지연·처리량이 중요하거나(센서 스트림), 버스 데몬이 없는 initramfs/복구 모드, 또는 MCU↔호스트처럼 D-Bus 가 없는 쪽. OpenBMC 의 D-Bus 구조는 BMC-M04 에서 (아직 안 씀).
</details>

<details><summary>Q7. 서버 스트림을 취소했는데 서버가 몇 개 더 보냈습니다. 버그인가요? K 를 0 으로 만들 수 있나요?</summary>

버그가 아니다. CANCEL 이 날아가는 동안 서버는 모른다 — 빛의 속도 문제와 같다. 그래서 클라이언트는 `cancel_sent` 이후 도착한 항목을 버리고 세며, 서버의 마지막 프레임(ERR CANCELLED 또는 이미 보낸 END)까지 기다려 "확실히 멈췄다" 를 안다. K 를 0 으로 만들 수는 없지만 **상한은 만들 수 있다**: 흐름 제어 window W 가 있으면 서버는 크레딧 없이는 못 보내므로 취소 후 추가 항목 ≤ W 가 보장된다 (연습 3). 서버 쪽에서는 지연을 `sleep` 대신 취소에 깨는 condvar 대기로 해야 반응이 빠르다 (`server_sleep`). 실측은 매번 0 개였다.
</details>

<details><summary>Q8. 이 RPC 를 1 Mbps UART 로 MCU 와 붙인다면 무엇을 바꾸겠나요?</summary>

① 프레이밍: 바이트가 깨지고 빠질 수 있으니 길이 prefix 대신 COBS(또는 SLIP) + CRC-16, 0x00 에서 재동기화 (COMM-B12, COMM-B11, 연습 1). 길이 prefix 를 유지한다면 헤더 CRC + 재동기화 탐색(COMM-B10, COMM-M10). ② 신뢰성: 소켓은 잃지 않지만 UART 는 잃는다 → 요청 재전송 + 중복 제거(COMM-M14 stop-and-wait ARQ, 연습 4). ③ 크기: 16 KiB 프레임·스레드 per slot 은 MCU 에 과하다 → 프레임 256 B, pending 4 개, condvar 대신 RTOS 이벤트 플래그/세마포어, 마감은 SysTick 기반. ④ 동시성: MCU 쪽은 보통 한 번에 하나 처리 + 응답 순서 = 요청 순서로 단순화. ⑤ 인코딩: 고정 TLV 대신 nanopb(protobuf for MCU) 나 NXP eRPC 같은 생성기 — 이름만 알고 써 보지는 않았다. **꼬리: 타임아웃 값은?** 왕복 바이트 시간(1 Mbps 에서 256 B ≈ 2.6 ms) + MCU 처리 최악 + 여유, 측정으로 정한다.
</details>

<details><summary>Q9. 이 클라이언트를 초당 100 만 호출로 키운다면 어디가 먼저 터지나요?</summary>

① **전역 mu** — 모든 호출과 reader 가 같은 락을 두 번씩 잡는다. req_id 를 slot 인덱스에 인코딩(`id = gen<<6 | idx`)하면 선형 탐색이 사라지고, slot 별 락 또는 lock-free 상태 전이로 쪼갤 수 있다. ② **호출당 스레드 깨우기 4 번** — 측정에서 1 호출 RTT 가 raw 핑퐁의 4~5 배였다. 비동기 API(콜백/future) + 이벤트 루프(kqueue/epoll, OS-M12/OS-A11) 로 스레드 전환을 없앤다. ③ **write 시스템콜 per frame** — 여러 프레임을 모아 `writev` 한 번(코크/배칭), 읽기도 큰 버퍼로 한 번 읽고 `frame_parse` 를 여러 번 (Part A 의 재조립 테스트가 바로 그 경로). ④ 서버의 job `malloc` → 슬랩/풀. **꼬리: 어떻게 증명하나?** demo_bench 처럼 처리량-지연 곡선을 찍고, 바꾼 하나마다 같은 부하에서 비교. 이 기계는 load 20 이상이라 비율만 본다.
</details>

## 9. 추가 연습

각 답은 `practice.c` 에 있다 (단일 스레드, 고정 시드 → 결정적).

1. **UART 링크 프레이밍.** RPC 프레임을 `COBS(payload ‖ CRC-16/CCITT-FALSE) ‖ 0x00` 로 보내고, 40 프레임 중 10 번째의 가운데 바이트 하나를 깨뜨린다. 같은 데이터를 `[u16 len][payload][crc]` 길이 prefix 로 보내면서 길이 바이트를 깨뜨린 것과 비교하라.

<details><summary>답</summary>

COBS 출력에는 0x00 이 없으므로 수신기는 0x00 을 보면 무조건 "프레임 끝, 새 시작" 으로 상태를 초기화한다. 깨진 프레임은 CRC 에서 떨어지고 그다음 프레임부터 정상이다. 실제 출력:

```text
      COBS: 받음 39, 버림 1 (깨진 프레임 10 번만 잃는다)
      길이 prefix: 받음 10 / 40 (깨진 길이 하나 뒤로 경계를 잃음)
```

길이 prefix 는 틀린 길이만큼 건너뛰는 순간 경계가 어긋나 나머지 30 개를 다 잃었다. 소켓에서는 이 일이 안 생기니 길이 prefix 가 맞고, UART 에서는 COBS 가 맞다. 비용: COBS 는 254 바이트마다 최대 1 바이트 오버헤드 (COMM-B12).
</details>

2. **protobuf 식 wire format.** key = `field << 3 | wire_type` 를 varint 로 쓰고, v1 디코더(필드 1, 2 만 앎)가 v2 메시지(필드 15 varint, 16 문자열, 17 fixed32, 18 fixed64 추가)를 읽게 하라. 11 바이트 varint, 잘린 fixed64, group(wire type 3) 은 거절.

<details><summary>답</summary>

우리 TLV 는 모든 필드가 `u32 len` 을 달고 다녀서 건너뛰기가 쉽지만 6 바이트 머리가 든다. protobuf 는 wire type 3 비트로 "다음 값의 길이를 아는 법" 만 싣는다: 0 = varint(끝 비트까지), 1 = 8 B, 5 = 4 B, 2 = 길이 varint + 바이트. 그래서 모르는 필드도 건너뛸 수 있고 작은 정수는 1 바이트다. 검증: 필드 1 = 150 → `08 96 01` (protobuf 문서의 예와 같음), v2 메시지에서 v1 이 `f1 = 150, f2 = "testing", unknown = 4`. zigzag(sint32) 도 확인: 0→0, −1→1, 1→2, −2→3. 정식 TLV/varint 수업은 DIST-B06 (아직 안 씀).
</details>

3. **크레딧 흐름 제어.** 보내는 쪽은 tick 마다 최대 4 개, 받는 쪽은 0~2 개(시드 고정) 소비. 초기 window W = 16, 받는 쪽이 W/2 이상 소비하면 WINDOW_UPDATE 로 그만큼 돌려준다. 받는 쪽 버퍼가 W 를 넘지 않음을 보이고 흐름 제어 없는 경우와 비교하라.

<details><summary>답</summary>

보낸 쪽은 크레딧이 있을 때만 보낸다 → 받는 쪽 미처리 개수 ≤ W 가 불변식이다. 실제 출력:

```text
      window 16: 받는 쪽 최대 버퍼 16, WINDOW_UPDATE 119 번 / 흐름 제어 없음: 최대 버퍼 768
```

1000 개 전부 순서대로 받았다. 흐름 제어가 없으면 버퍼가 768 까지 자랐다 (소비가 느린 만큼 끝없이). 업데이트 빈도(W/2)는 제어 프레임 수와 처리량 사이의 절충이다. HTTP/2 는 같은 방식을 스트림별 + 연결 전체 두 층으로 한다 (초기 window 65535, RFC 9113 — 기억 기반). 본문 Q7 의 "취소 후 추가 항목 ≤ W" 가 이것으로 보장된다.
</details>

4. **재시도 + 중복 제거 = at-most-once 실행.** 입금 RPC 200 건을 요청 유실 10 %, 응답 유실 30 % 링크로 보낸다. 클라이언트는 응답을 받을 때까지 **같은 req_id** 로 재시도한다. 서버의 중복 제거 표가 있을 때와 없을 때 잔액을 비교하고, 표가 끝없이 자라지 않게 하라.

<details><summary>답</summary>

```text
      중복 제거 없음: 재시도 141, 실행 305 번, 잔액 1525 (기대 1000)
      중복 제거 있음: 재시도 141, 실행 200 번, 캐시 응답 105 번, 표 최대 1 칸
```

응답이 유실되면 서버는 이미 실행했다 → 재시도가 두 번째 입금이 된다. 표는 `(client, req_id) → 저장된 응답` 이고 같은 키가 오면 실행 없이 저장된 응답을 돌려준다. 표 정리: 클라이언트가 요청에 `ack_upto`(여기까지 응답 받음) 를 실어 보내면 서버가 그 이하를 지운다 → 표 크기 ≤ 동시 진행 요청 수. Birrell & Nelson(1984) RPC 논문 이후의 고전적 방식이고, 서버가 재시작하면 표가 사라진다는 한계가 있다 (영구 저장 또는 epoch 필요).
</details>

5. **마감 전파.** `child_deadline_ms(parent_remaining, own_default, margin)` = `min(parent_remaining − margin, own_default)`, 0 이하면 보내지 않고 즉시 DEADLINE_EXCEEDED. 단계마다 여유 5 ms + 일 40 ms 를 쓰는 호출 사슬이 500 ms 예산에서 몇 단까지 가나?

<details><summary>답</summary>

500 → 455 → 410 → … → 50 → 5 로 단계마다 45 ms 씩 줄고, 11 단 뒤 남은 예산이 0 이 되어 12 번째는 보내지 않는다 (`depth == 11`). 핵심은 **아래 단계가 위 단계보다 긴 마감을 갖지 않는 것** 이다. 그렇지 않으면 위에서 이미 포기한 요청을 아래가 계속 처리한다 (본문 서버가 타임아웃 난 ECHO 를 끝까지 하는 낭비와 같은 문제). margin 은 응답이 돌아오는 시간과 시계 오차 몫이다.
</details>

6. **req_id 할당기 랩어라운드.** 16 비트로 줄인 id 공간에서 0xFFF0 부터 시작해 40 개를 할당(반은 즉시 해제)하라. 0 은 건너뛰고, 다시 같은 구간을 돌 때 쓰는 중인 id 를 피하고, 공간이 꽉 차면 무한 루프 대신 실패해야 한다.

<details><summary>답</summary>

`do { next = (next + 1) & mask; } while (next == 0 || used[next]);` 만으로는 꽉 찬 공간에서 영원히 돈다. 그래서 루프 전에 `inuse >= mask` 면 실패한다. 검사: 첫 id 0xFFF1, 15 번째 0xFFFF 다음이 1 (0 건너뜀), 두 번째 바퀴 10 개가 쓰는 중인 20 개와 안 겹침, 65535 개를 다 채운 뒤 `false`. 본문 `slot_alloc` 은 쓰는 중인 id 가 64 개뿐이라 32 비트 공간에서 꽉 찰 일이 없지만, 같은 코드를 8 비트 시퀀스(작은 MCU 프로토콜)로 옮기면 바로 이 문제가 된다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
```

실제 결과: `24/24 passed` (ASan+UBSan 빌드도 `24/24 passed`, stderr 없음).

## 10. 핵심 정리

- 스트림 위 RPC 의 첫 일은 프레이밍이다. 길이는 몸통보다 먼저 범위 검사, TLV 길이는 "남은 바이트" 와 비교. 모르는 type/tag 는 건너뛰고, 깨진 길이는 연결을 끊는다.
- 부분 읽기/쓰기는 1 바이트 shim 으로 테스트에서 강제한다. 디코더는 독립 참조 구현과 퍼즈로 비교한다.
- 다중화 = reader 1 개 + pending 표(req_id → slot + 자기 condvar) + 프레임 단위 쓰기 락. **등록 → 보내기 → 기다리기** 순서.
- 마감은 MONOTONIC 으로 재고 macOS 에선 REALTIME 절대 시각으로 바꿔 기다린다. 타임아웃이면 즉시 지우고, 늦은 응답은 버리고 센다. 응답 없음 ≠ 실행 안 됨.
- 취소는 경주다: 보내기 전에 표시, 그 뒤 항목은 버림, 서버의 마지막 프레임으로 멈춤 확인. 상한은 흐름 제어 window 가 준다.
- 상대가 죽으면 reader 만 안다 → `fail_all`. SIGPIPE 는 `SO_NOSIGPIPE`/`MSG_NOSIGNAL`. 끝에는 fd 수와 스레드 수로 누수 없음을 확인.
- 하위 호환 = 번호 재사용 금지 + 없으면 기본값 + 모르면 건너뜀. 의미 변경·필수 필드는 이걸로 안 풀린다.
- 측정: 1 호출 RTT 는 raw 핑퐁의 4~5 배(스레드 경계 4 번), 동시 호출로 처리량 2~3 배. 공유 cv broadcast → 큐 전용 cv signal 로 p50 약 62 → 23 µs.

## 참고자료

- OSTEP 47 장 *Distributed Systems* — 47.5 Remote Procedure Call (스텁, 런타임, 타임아웃/재시도, 엔디언), 47.4 Communication Abstractions
- A. D. Birrell, B. J. Nelson, *Implementing Remote Procedure Calls*, ACM TOCS 2(1), 1984 (OSTEP [BN84])
- RFC 9113 *HTTP/2* — 프레임 형식, 스트림 id, RST_STREAM, WINDOW_UPDATE, 모르는 프레임 type 무시 (기억 기반 인용, 원문 대조 안 함)
- gRPC over HTTP/2 프로토콜 문서 (`PROTOCOL-HTTP2.md`, grpc/grpc 저장소) — Length-Prefixed-Message, `grpc-timeout`; gRPC 상태 코드 (CANCELLED 1, RESOURCE_EXHAUSTED 8, UNIMPLEMENTED 12)
- Protocol Buffers *Encoding* 문서 — varint, wire type, `08 96 01` 예
- D-Bus Specification (freedesktop.org) — serial, REPLY_SERIAL, 메시지 종류 (기억 기반, 검증 안 함)
- POSIX.1-2017 `pthread_cond_timedwait`, `read`, `write`, `shutdown` (pubs.opengroup.org)
- macOS `man 2 setsockopt` (SO_NOSIGPIPE), `man 2 shutdown` (`man 4 unix` 는 이 맥에 없음); SDK 26.4 헤더 `sys/socket.h` (`MSG_NOSIGNAL 0x80000`), `sys/un.h` (`sun_path[104]`), `pthread/pthread.h` (`pthread_cond_timedwait_relative_np`, `pthread_condattr_setclock` 없음), `sys/errno.h` (`ECANCELED 89`); Linux `asm-generic/errno.h` (`ECANCELED 125`, Lima VM 에서 확인)
- 관련 레슨: OS-B06(condvar), OS-B07(시그널·SIGPIPE), OS-B14(IPC 비교), OS-M02(문맥 전환 비용), OS-M12(I/O 모델), OS-M16(Unix 소켓 심화), OS-A11(서비스 데몬), OS-A12(공유 메모리 IPC 의 상대 장애 감지), OS-A14(Mach 메시지·XPC, Binder 비교), C-B13(엔디언), COMM-B10(길이+CRC 프레임 파서), COMM-B11(SLIP), COMM-B12(COBS), COMM-M10(UBX 재동기화), COMM-M14(stop-and-wait ARQ)
- 다음 단계 (아직 안 씀): DIST-B06(Varint·ZigZag·TLV), BMC-M04(OpenBMC D-Bus), GPUSYS-M05(GPU 펌웨어 RPC)
