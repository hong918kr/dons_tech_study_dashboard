---
id: OS-A14
time: 120분
updated: 2026-11-15
---
# OS-A14 macOS Mach 메시지와 XPC: 포트 권한 기반 IPC

## 학습 목표

- Mach 포트의 **receive right / send right / send-once right** 가 무엇이고, 포트 "이름(name)" 이 왜 태스크마다 다른 숫자인지 `mach_port_get_refs` 실측으로 설명한다.
- `mach_msg` 요청/응답을 직접 짠다: 요청에 `MAKE_SEND_ONCE` 응답 권리를 싣고, 서버는 그 권리로 **딱 한 번** 답한다. 타임아웃 뒤 늦은 응답이 다음 호출을 오염시키지 않게 응답 포트를 갈아 끼운다(MIG 방식).
- 복합 메시지(`MACH_MSGH_BITS_COMPLEX`)로 **권리(포트)와 큰 메모리(OOL)** 를 보내고, OOL 이 "보낸 순간의 스냅샷(가상 복사)" 임을 테스트로 증명한다.
- 상대가 죽은 것을 **커널이 알려 주는** 세 가지 길(dead-name, no-senders, send-once 알림)을 결정적 테스트로 보이고, pid 기반 감지와 무엇이 다른지 말한다.
- launchd 가 서비스 이름을 포트에 잇는 방식, XPC 의 층 구조(딕셔너리 → libxpc → Mach)를 그리고, 리눅스 Binder(단일 복사·객체 참조·death notification)와 비교한다. 지연은 mach_msg / pipe / Unix 소켓 / XPC 를 실측으로 비교한다.

## 1. 문제

Apple 플랫폼의 모든 IPC(XPC, MIG 로 만든 커널 인터페이스, 예외 처리, launchd 서비스)는 Mach 포트 위에 있다. Apple 면접의 시스템 질문이 "fd 대신 **권리(capability)** 를 주고받는 IPC 는 무엇이 다른가?" 로 자주 흘러가는 이유다. 펌웨어 쪽에서 보면 RTOS 큐 핸들(RTOS-B11)이나 seL4/Zircon 의 채널 핸들과 같은 사상이다 — "주소를 알면 보낼 수 있다" 가 아니라 "권리를 받아야 보낼 수 있다".

POSIX IPC(OS-B14)와의 큰 차이 세 가지:

| | POSIX (pipe, Unix 소켓) | Mach 포트 |
|---|---|---|
| 찾기 | 파일 경로, 상속된 fd | 권리를 **메시지로 받아야만** 생긴다. 이름 서비스 = launchd |
| 단위 | 바이트 스트림 (경계 없음) | 메시지 (헤더 + 본문 + 권리 + 메모리) |
| 상대 사망 | EOF / EPIPE (읽거나 쓸 때 안다) | 커널이 **알림 메시지** 를 보낸다 (dead-name, no-senders, send-once) |

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">포트는 커널 객체, 이름은 태스크별 번호, 권리는 이름에 붙은 표</text>
  <rect x="10" y="40" width="220" height="200" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="20" y="62" fill="currentColor" font-weight="bold">태스크 A (서버) 이름 공간</text>
  <text x="20" y="88" fill="currentColor" font-family="monospace">0x1e03 RECEIVE + SEND(2)</text>
  <text x="20" y="110" fill="currentColor" font-family="monospace">0x1d03 SEND_ONCE(1)</text>
  <text x="20" y="132" fill="currentColor" font-family="monospace">0x0c03 DEAD_NAME(2)</text>
  <text x="20" y="160" fill="currentColor">이름 = 인덱스 + 세대 바이트</text>
  <text x="20" y="180" fill="currentColor">uref = 같은 권리의 참조 수</text>
  <text x="20" y="200" fill="currentColor">receive right 는 포트당 1 개</text>
  <text x="20" y="220" fill="currentColor">(받는 태스크도 1 곳)</text>
  <rect x="270" y="60" width="200" height="150" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="280" y="82" fill="currentColor" font-weight="bold">커널: 포트 객체</text>
  <text x="280" y="104" fill="currentColor">메시지 큐 (qlimit 기본 5)</text>
  <text x="280" y="124" fill="currentColor">receiver = 태스크 A</text>
  <text x="280" y="144" fill="currentColor">send 권리 수, make-send 수</text>
  <text x="280" y="164" fill="currentColor">알림 요청 (dead-name,</text>
  <text x="280" y="184" fill="currentColor">no-senders)</text>
  <rect x="510" y="40" width="220" height="200" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="520" y="62" fill="currentColor" font-weight="bold">태스크 B (클라이언트)</text>
  <text x="520" y="88" fill="currentColor" font-family="monospace">0x0b03 SEND(1)</text>
  <text x="520" y="110" fill="currentColor" font-family="monospace">0x1e23 RECEIVE (응답 포트)</text>
  <text x="520" y="140" fill="currentColor">같은 포트인데 이름이 다르다</text>
  <text x="520" y="160" fill="currentColor">(A 의 0x1e03 = B 의 0x0b03)</text>
  <text x="520" y="190" fill="currentColor">이름을 숫자로 다른 태스크에</text>
  <text x="520" y="210" fill="currentColor">보내 봐야 소용없다</text>
  <path d="M230 90 L266 110" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M256 110 L268 111 L260 101" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M510 90 L474 110" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M484 101 L472 111 L484 113" stroke="currentColor" stroke-width="2" fill="none"/>
  <g fill="currentColor">
    <text x="10" y="270">권리를 옮기는 유일한 길 = 메시지 (헤더의 reply 필드, 포트 디스크립터). 커널이 받는 쪽 이름을 새로 붙인다.</text>
    <text x="10" y="292">MAKE_SEND / MAKE_SEND_ONCE: receive right 가진 자만 · COPY_SEND: send 를 복제 · MOVE_*: 넘겨주고 잃는다</text>
    <text x="10" y="314">receive right 가 사라지면 그 포트의 send right 는 모두 dead name (알림을 걸었으면 메시지)</text>
  </g>
</svg>
```

만들 것: `solution.c` 한 파일. 테스트는 같은 프로세스의 스레드 사이, 그리고 `fork` 한 자식과의 사이에서 돈다. 파일·소켓 경로·이름 서비스 항목을 남기지 않는다.

### Part A — 권리와 요청/응답
- `port_create`: receive right 1 + 같은 이름의 send right 1 (uref 확인).
- `server_reply`: send-once right 로 응답 1 개. 두 번째 사용은 `MACH_SEND_INVALID_DEST`.
- `server_handle_one`: 수신 → 형식 검사(id, 크기, 비복합, 응답 권리 종류) → op 처리 → 응답. 불량 요청은 `mach_msg_destroy`.
- `rpc_call`: 요청 + 응답 대기. 서버가 응답 권리를 버리면 `MIG_SERVER_DIED`, 타임아웃이면 응답 포트 교체.
- 검사: 1000 회 + 4 스레드 동시 호출, send-once 소비, 서버 drop, 늦은 응답.

### Part B — 복합 메시지와 알림
- `send_complex` / `recv_complex`: 포트 디스크립터 + OOL 디스크립터 1 개씩. 형식이 틀리면 거부하고 실려 온 권리는 반납.
- `arm_notify` / `wait_notify`: `MACH_NOTIFY_NO_SENDERS`, `MACH_NOTIFY_DEAD_NAME`, 수신 타임아웃.
- 검사: uref 증가(같은 태스크면 같은 이름), OOL 스냅샷 의미론, 마지막 send right 에서만 no-senders, dead name uref 2 개 정리, 이름 누수 0.

### Part C — 프로세스 경계, launchd, XPC
- `spawn_child_server`: fork 한 자식과 send right 를 교환한다. 포트는 fork 로 상속되지 않는다 — 특수 포트(`TASK_BOOTSTRAP_PORT`) 하나만 상속된다는 점을 이용한다.
- 자식을 정상 종료/`SIGKILL` → 부모가 dead-name 알림으로 안다.
- `xpc_server_start` / `xpc_call`: 익명 리스너 + 엔드포인트, 딕셔너리 요청/응답, 취소 시 `XPC_ERROR_CONNECTION_INVALID`.
- 관찰(demo): launchd 이름 조회, XPC 가 쓰는 Mach 포트 수, 지연 비교.

## 2. Problem (English)

Implement a small Mach-port RPC layer and an XPC equivalent on macOS.

```c
kern_return_t port_create(mach_port_t *out);                      /* receive right + one send right, same name */
kern_return_t server_reply(mach_port_t reply_so, uint32_t seq, int32_t status, int64_t value);
kern_return_t server_handle_one(mach_port_t port, mach_msg_timeout_t ms, server_stats_t *st);
kern_return_t rpc_call(rpc_client_t *c, uint32_t op, int64_t a, int64_t b, int64_t *out, mach_msg_timeout_t ms);
kern_return_t send_complex(mach_port_t dest, mach_port_t right, mach_msg_type_name_t disp,
                           const void *buf, size_t len, uint32_t tag);   /* port descriptor + OOL descriptor */
kern_return_t recv_complex(mach_port_t port, mach_msg_timeout_t ms, cx_got_t *out);
kern_return_t arm_notify(mach_port_t name, mach_msg_id_t which, mach_port_t notify_port);
kern_return_t wait_notify(mach_port_t notify_port, mach_msg_timeout_t ms, notify_got_t *out);
pid_t         spawn_child_server(mach_port_t *child_send, cx_got_t *hello);
int           xpc_server_start(xsrv_t *s);
int           xpc_call(xpc_connection_t c, const char *op, int64_t a, int64_t b, int64_t *out);
```

Requirements:
- A request carries a send-once reply right; the server answers exactly once. If the server destroys the reply right without answering, the client must get `MIG_SERVER_DIED` (from the kernel's send-once notification), not hang.
- A client receive timeout returns `MACH_RCV_TIMED_OUT` and replaces the reply port, so a late reply can never be taken as the answer to a later call.
- Malformed requests and malformed complex messages are rejected and every right/memory region they carried is released (no port-name leaks: the task's name count is identical before and after the Mach tests).
- Detect peer death with dead-name notifications, including across `fork` and `SIGKILL`.
- XPC: an anonymous listener + endpoint in-process; `{op, a, b}` → `{value}` or `{error}`; after `xpc_connection_cancel` calls return `XPC_ERROR_CONNECTION_INVALID`.

Example: `rpc_call(&c, OP_MUL, 6, 7, &v, 2000)` → `KERN_SUCCESS`, `v == 42`; `rpc_call(&c, OP_DROP, ...)` → `MIG_SERVER_DIED`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st
```

추가 빌드 플래그는 없다: libxpc·libdispatch 는 libSystem 안에 있고(`-framework` 불필요, 확인함), Apple clang 은 `-std=c11` 에서도 블록(`^{}`)을 켠다. TODO 는 11 개다(A 4, B 4, C 3). `child_main`(자식 몸체), 서버 스레드, 이름 개수 세기(`mach_port_names`)는 하네스가 준다.

시작 상태의 실제 실행 (0.14 초, 종료 코드 1):

```text
--- A1: port_create — 권리와 uref ---
FAIL  kr == KERN_SUCCESS && p != MACH_PORT_NULL   (line 291)
...
--- C1: fork + TASK_BOOTSTRAP_PORT 로 자식과 send right 교환 ---
PASS  after == saved
...
FAIL  rc == 0 && g_xs.listener != NULL   (line 575)
2/58 passed
```

모든 수신에 `MACH_RCV_TIMEOUT` 이 있고, 스텁이 실패를 돌려주면 서버 스레드는 끝나며, 전체 하네스에 `alarm(60)` 이 있다. 틀린 코드는 멈추지 않고 FAIL 한다.

추천 순서: `port_create` → `server_reply` → `server_handle_one` → `rpc_call` (Part A 통과) → `send_complex` / `recv_complex` → `arm_notify` / `wait_notify` → `spawn_child_server` → XPC 두 함수.

## 4. 힌트

<details><summary>힌트 1 — 헤더 비트: 보낼 때와 받을 때 뜻이 바뀐다</summary>

보낼 때 `MACH_MSGH_BITS(remote, local)` 의 두 값은 "이 이름의 권리를 **어떻게** 넘길지" 다: 요청은 `(COPY_SEND, MAKE_SEND_ONCE)`. 받았을 때는 커널이 remote/local 을 **맞바꿔서** 채운다. 수신자 기준으로 `msgh_remote_port` 가 응답할 곳(send-once 이름), `msgh_local_port` 가 내가 받은 포트다. `MACH_MSGH_BITS_REMOTE(bits)` 는 이제 "받은 권리의 종류" 로 `MACH_MSG_TYPE_PORT_SEND_ONCE`(18) 이어야 한다.
</details>

<details><summary>힌트 2 — 수신 버퍼 크기</summary>

커널은 메시지 뒤에 트레일러(기본 8 B)를 붙인다. 버퍼를 `sizeof(msg)` 로 잡으면 `MACH_RCV_TOO_LARGE` 이고, 그 메시지는 **파괴된다**(`MACH_RCV_LARGE` 를 안 줬을 때). `msg + mach_msg_max_trailer_t` 크기의 union 을 쓴다. 알림 메시지도 같은 포트로 올 수 있으니 그보다 작으면 안 된다.
</details>

<details><summary>힌트 3 — 서버가 응답하지 않고 돌아가면?</summary>

받은 send-once right 를 그냥 두면 클라이언트는 영원히 기다린다(이름 누수이기도 하다). 버리려면 `mach_port_deallocate` 나 `mach_msg_destroy` — 커널은 send-once right 가 쓰이지 않고 사라질 때 그 포트로 `MACH_NOTIFY_SEND_ONCE`(id 71) 메시지를 대신 보낸다. 클라이언트는 그 id 를 보고 `MIG_SERVER_DIED` 를 돌려주면 된다.
</details>

<details><summary>힌트 4 — 타임아웃 뒤 응답 포트</summary>

요청은 이미 서버 큐에 있다. 서버가 나중에 처리하면 응답이 같은 응답 포트에 쌓인다 → 다음 호출이 그걸 자기 응답으로 읽는다. 해법: 새 receive right 를 **먼저** 만들고 옛것을 부순다(이름이 반드시 달라진다). 늦은 응답은 서버 쪽에서 `MACH_SEND_INVALID_DEST` 가 되고, 서버는 남은 dead name 을 지워야 한다.
</details>

<details><summary>힌트 5 — 복합 메시지 배치</summary>

`header → mach_msg_body_t{descriptor_count} → 디스크립터들 → 인라인 데이터`. 비트에 `MACH_MSGH_BITS_COMPLEX` 를 꼭 넣는다. 받는 쪽은 비트·개수·각 `type` 을 검사하고, 하나라도 다르면 `mach_msg_destroy(&hdr)` 가 실려 온 권리와 OOL 메모리를 반납한다.
</details>

<details><summary>힌트 6 — 포트를 자식에게 주는 법</summary>

fork 는 포트 이름 공간을 복사하지 않는다. 대신 태스크 특수 포트(`TASK_BOOTSTRAP_PORT`)는 자식이 상속한다. fork 직전에 그걸 내 랑데부 포트로 바꾸고, fork 직후 부모는 **원래 값으로 복원**한다. 자식은 `task_get_special_port` 로 랑데부 포트를 얻어 자기 서비스 포트를 `MAKE_SEND` 디스크립터로 보낸다.
</details>

## 5. 풀이

### Part A — 권리와 요청/응답

**권리 실측 (A1, B1 테스트가 찍은 값).**

| 시점 | 이름 | RECEIVE uref | SEND uref | 비고 |
|---|---|---|---|---|
| `mach_port_allocate(RECEIVE)` 직후 | 0xb03 | 1 | 0 | type 0x20000 |
| `insert_right(MAKE_SEND)` | 0xb03 | 1 | 1 | type 0x30000 = RECEIVE\|SEND |
| 같은 send right 를 `COPY_SEND` 디스크립터로 나에게 보냄, 아직 큐에 있음 | | | 1 | 메시지 안에 따로 1 개 |
| 받은 뒤 | **같은 이름** | 1 | 2 | 한 태스크 안에선 같은 포트의 send right 가 한 이름으로 합쳐진다 |
| `mach_port_deallocate` | | 1 | 1 | |

"이름" 은 포인터가 아니라 그 태스크의 권리 표 인덱스다. 끝 바이트(`..03`, `..0f`, `..23`)는 재사용 세대 비트로 보인다: 같은 슬롯을 다시 쓰면 숫자가 바뀌어, 옛 이름을 실수로 다시 쓰는 버그를 줄여 준다(XNU 소스로 확인하지는 않았다 — 관찰).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">요청/응답: 응답 권리는 send-once — 쓰거나 버리면 사라진다</text>
  <line x1="120" y1="40" x2="120" y2="290" stroke="currentColor"/>
  <line x1="620" y1="40" x2="620" y2="290" stroke="currentColor"/>
  <text x="70" y="38" fill="currentColor" font-weight="bold">클라이언트</text>
  <text x="590" y="38" fill="currentColor" font-weight="bold">서버</text>
  <rect x="140" y="52" width="460" height="46" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="150" y="70" fill="currentColor" font-family="monospace">bits=(COPY_SEND, MAKE_SEND_ONCE) remote=srv local=reply</text>
  <text x="150" y="90" fill="currentColor" font-family="monospace">id=1400 {op, seq, a, b}</text>
  <path d="M120 100 L616 112" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M606 106 L618 112 L606 118" stroke="currentColor" stroke-width="2" fill="none"/>
  <rect x="140" y="128" width="460" height="46" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="150" y="146" fill="currentColor">받은 쪽: remote = send-once 이름, REMOTE 비트 = 18</text>
  <text x="150" y="166" fill="currentColor">local = 서버 포트. 형식이 틀리면 mach_msg_destroy</text>
  <rect x="140" y="186" width="460" height="28" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="150" y="205" fill="currentColor" font-family="monospace">응답 bits=(MOVE_SEND_ONCE, 0) id=1500 {seq, status, value}</text>
  <path d="M620 220 L124 232" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M134 226 L122 232 L134 238" stroke="currentColor" stroke-width="2" fill="none"/>
  <rect x="140" y="246" width="460" height="40" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="150" y="263" fill="currentColor">응답 없이 권리를 버리면 → 커널이 id 71 (SEND_ONCE 알림)을 대신 보냄</text>
  <text x="150" y="280" fill="currentColor">→ 클라이언트는 MIG_SERVER_DIED. 두 번째 응답은 MACH_SEND_INVALID_DEST</text>
</svg>
```

헤더 상수는 SDK 26.4 의 `mach/message.h` 에서 확인했다: `MACH_MSG_TYPE_MAKE_SEND 20`, `MAKE_SEND_ONCE 21`, `MOVE_SEND_ONCE 18`, `COPY_SEND 19`(240–249 행), `MACH_MSGH_BITS_COMPLEX 0x80000000U`(149 행), `MACH_RCV_TIMED_OUT 0x10004003`(875 행). 타임아웃 단위는 밀리초다 — 헤더 90–95 행 주석: *"The timeout units are milliseconds. It is controlled with the MACH_SEND_TIMEOUT and MACH_RCV_TIMEOUT options."*

**A3 실측: 같은 send-once 이름으로 두 번 답하기.**

```text
      first reply kr=0x0, name after=KERN_INVALID_NAME, second reply kr=0x10000003 ((ipc/send) invalid destination port)
```

첫 응답이 권리를 **소비**하면서 이름 자체가 이름 공간에서 사라진다. 두 번째 호출은 존재하지 않는 이름이라 `MACH_SEND_INVALID_DEST`. 주의: 그 사이에 같은 태스크가 새 권리를 만들면 같은 숫자가 다른 포트를 가리킬 수도 있다(파일 디스크립터 재사용과 같은 함정). 세대 비트가 확률을 낮출 뿐이다.

**A4: 응답하지 않는 서버.** `OP_DROP` 에서 서버는 `mach_port_deallocate(remote)` 만 한다. 커널이 클라이언트 응답 포트로 id 71(`MACH_NOTIFY_SEND_ONCE`, `mach/notify.h` 85 행)을 보낸다. 형식이 틀린 요청도 `mach_msg_destroy` 가 같은 일을 한다. 그래서 "서버가 처리 도중 죽음" 이 클라이언트에겐 **시간 초과가 아니라 즉시 오류**로 보인다. MIG 생성 코드가 `MIG_SERVER_DIED`(-308, `mig_errors.h` 90 행)를 돌려주는 경로와 같은 생각이다.

**A5: 타임아웃.** 클라이언트가 30 ms 뒤 포기 → 응답 포트를 새로 만든다(새것 먼저, 옛것 파괴 → 이름이 반드시 다르다) → 서버가 뒤늦게 옛 요청을 처리 → `server_reply` 가 `MACH_SEND_INVALID_DEST` 를 받는다. 이때 응답 권리는 **dead name 으로 남아 있다**(실험: 죽은 send-once 로 보내기 실패 뒤에도 dead uref 1). 지우지 않으면 서버가 타임아웃 하나마다 이름 하나씩 샌다. 그래서 `server_reply` 가 실패 시 `mach_port_deallocate` 한다. 테스트는 `st.reply_failed == 1`, 그다음 호출의 `stale == 0`, 마지막에 이름 개수 동일을 본다.

seq 비교(`stale` 카운터)는 방어용이다: 응답 포트를 갈아 끼우면 다른 호출의 응답이 섞일 일이 없지만, 그 불변식이 깨졌을 때 오답 대신 버리도록.

### Part B — 복합 메시지와 알림

메시지 배치 (`sizeof` 실측 60 B: 헤더 24 + body 4 + 포트 디스크립터 12 + OOL 디스크립터 16 + tag 4):

| 필드 | 보낼 때 | 받을 때 |
|---|---|---|
| `msgh_bits` | `(COPY_SEND, 0) \| COMPLEX` | `0x80001100` (COMPLEX, local = PORT_SEND) |
| `port.name / disposition` | 넘길 이름 / `COPY_SEND`·`MAKE_SEND`·`MOVE_*` | **받는 태스크의** 이름 / `PORT_SEND`(17) 등 |
| `ool.address / size` | 내 버퍼 / 길이 | 커널이 새로 매핑한 주소 / 같은 길이 |
| `ool.copy` | `MACH_MSG_VIRTUAL_COPY` | |

**OOL 은 스냅샷이다 (B2 실측).**

```text
      sender=0x104730000 receiver=0x109224000 len=1048576 page=16384 mismatched words=0
```

보낸 뒤·받기 전에 원본을 고쳤는데 수신자는 **고치기 전** 내용을 본다. 주소는 다르고 16 KiB 페이지에 정렬돼 있다. 즉 커널은 보내는 순간 그 영역을 copy-on-write 로 묶어 두고, 받는 태스크에 새 매핑을 만든다. 데이터는 아무도 쓰지 않는 한 복사되지 않는다 — OS-M03 의 fork COW 와 같은 장치다. XNU 내부 함수 이름(vm_map_copyin 등)은 소스를 열어 보지 않았으므로 여기선 관찰만 말한다. 받는 쪽은 그 영역의 주인이 되니 `vm_deallocate` 해야 한다(안 하면 VM 누수).

**알림 세 가지.**

| 알림 | id (`notify.h`) | 거는 대상 | 언제 오나 | 실측 |
|---|---|---|---|---|
| no-senders | 70 | 내 receive right | send right 가 전부 사라질 때 | uref 2→1: 안 옴(20 ms 타임아웃), 1→0: 옴, mscount=2 |
| send-once | 71 | (자동) | 쓰이지 않은 send-once 가 파괴될 때 | A4 |
| dead-name | 72 | 내 send right | 그 포트의 receive right 가 사라질 때 | B4, C1, C2 |

no-senders 는 **서버** 쪽 도구다: "이 세션을 쓰는 클라이언트가 하나도 안 남았다 → 자원 정리". dead-name 은 **클라이언트** 쪽 도구다: "서버가 죽었다". 둘 다 상대 pid 를 몰라도 되고, pid 재사용과 무관하다 — 포트가 곧 신원이기 때문이다. OS-A12 가 공유 메모리에서 하트비트 + `kill(pid, 0)` 로 애써 하는 일을 커널이 대신 한다.

**dead name 의 uref 가 2 인 이유 (B4 실측).**

```text
      dead name 0x1e1f urefs=2 (send right 1 + 알림이 실어 온 1)
```

dead-name 알림 메시지가 그 이름에 대한 uref 를 하나 **더 실어 온다**. 그래서 정리는 `mach_port_deallocate` 두 번이다. 한 번만 하면 이름이 영원히 남는다 — 오래 사는 데몬에서 dead name 이 쌓이는 전형적인 누수다. 테스트는 두 번 뒤 `KERN_INVALID_NAME` 을 본다.

**누수 검사.** 하네스는 Part A–C(Mach) 앞뒤로 `mach_port_names` 개수를 비교한다. 실측 `port names: start=11 end=11` (TSan 빌드에선 런타임이 첫 스레드 생성 때 포트 하나를 만들어서, 측정 전에 빈 스레드 하나를 먼저 돌린다).

### Part C — 프로세스 경계

**fork 와 포트.** fork 한 자식은 부모의 포트 이름들을 받지 않는다(태스크 = 새 이름 공간). 상속되는 것은 태스크 특수 포트(bootstrap, 등)뿐이다. 그래서 고전적 트릭:

1. `task_get_special_port(TASK_BOOTSTRAP_PORT)` 로 원래 값 저장
2. 랑데부 포트를 `task_set_special_port` 로 bootstrap 자리에 넣기
3. `fork`
4. 부모: **즉시 원래 값 복원** (실패 경로 포함). 테스트 `after == saved` 가 이걸 본다.
5. 자식: bootstrap 자리의 포트(=부모 랑데부)로 자기 서비스 포트를 `MAKE_SEND` 디스크립터 + 64 KiB OOL 로 보낸다.

실측 (C1):

```text
      child pid=54469, child's port seen here as 0x1e23, dead-name id=72, exit=0
```

자식이 `OP_QUIT` 에 답하고 `_exit(0)` → 그 태스크의 receive right 가 사라짐 → 부모의 send right 가 dead name 이 되고 알림 72 가 온다 → 이후 `rpc_call` 은 `MACH_SEND_INVALID_DEST`. C2 는 `SIGKILL` 로 같은 것을 본다: 프로세스가 어떻게 죽든 커널이 포트를 정리하니 알림은 온다(SIGKILL 이라 크래시 리포트는 생기지 않는다).

자식에서 bootstrap 자리는 부모 랑데부 포트로 남아 있으므로, 자식은 launchd 조회나 XPC 를 쓸 수 없다(fork 뒤 libxpc/libdispatch 는 어차피 쓰면 안 된다). 제품 코드에서 같은 일을 하는 지원 API 는 `posix_spawnattr_setspecialport_np`(`spawn.h` 160 행)다 — spawn 한 프로세스에만 특수 포트를 넣는다. Chromium 이 예전에 이런 방식으로 자식과 포트를 교환했다고 알고 있으나 확인하지 않았다.

**launchd: 이름 → 포트.**

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">launchd 가 서비스 이름에 포트를 잇는 방식 (MachServices)</text>
  <rect x="10" y="40" width="200" height="90" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="20" y="62" fill="currentColor" font-weight="bold">launchd.plist</text>
  <text x="20" y="84" fill="currentColor" font-family="monospace">MachServices = {</text>
  <text x="20" y="102" fill="currentColor" font-family="monospace"> "com.x.svc" = true }</text>
  <text x="20" y="120" fill="currentColor">로드 시 launchd 가 읽음</text>
  <rect x="270" y="40" width="200" height="110" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="280" y="62" fill="currentColor" font-weight="bold">launchd (bootstrap)</text>
  <text x="280" y="84" fill="currentColor">이름 표: com.x.svc →</text>
  <text x="280" y="104" fill="currentColor">포트 (receive right 를</text>
  <text x="280" y="124" fill="currentColor">launchd 가 보관 = endpoint)</text>
  <text x="280" y="144" fill="currentColor">잡이 안 떠 있어도 존재</text>
  <rect x="530" y="40" width="200" height="90" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="540" y="62" fill="currentColor" font-weight="bold">클라이언트</text>
  <text x="540" y="84" fill="currentColor" font-family="monospace">bootstrap_look_up</text>
  <text x="540" y="104" fill="currentColor">→ send right 받음</text>
  <text x="540" y="124" fill="currentColor">→ 바로 메시지 전송</text>
  <path d="M210 85 L266 85" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M256 79 L268 85 L256 91" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M530 95 L474 95" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M484 89 L472 95 L484 101" stroke="currentColor" stroke-width="2" fill="none"/>
  <rect x="270" y="190" width="200" height="80" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="280" y="212" fill="currentColor" font-weight="bold">서비스 잡 (on-demand)</text>
  <text x="280" y="232" fill="currentColor">메시지가 큐에 오면 기동</text>
  <text x="280" y="252" fill="currentColor" font-family="monospace">bootstrap_check_in</text>
  <text x="280" y="268" fill="currentColor">→ receive right 넘겨받음</text>
  <path d="M370 150 L370 186" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M364 176 L370 188 L376 176" stroke="currentColor" stroke-width="2" fill="none"/>
  <g fill="currentColor">
    <text x="490" y="190">잡이 죽으면 receive right 는</text>
    <text x="490" y="210">launchd 로 되돌아감 → 같은 포트</text>
    <text x="490" y="230">계속 광고, 클라이언트는 모름</text>
    <text x="490" y="250">(ResetAtClose=true 면 dead-name)</text>
  </g>
</svg>
```

근거: `man 3 bootstrap_check_in` (launch(3), 27.2 SDK man 경로): *"launchd(8) creates this port and advertises it in the appropriate Mach bootstrap namespace by parsing the MachServices entry of the job's launchd.plist(5)."* 와 *"If the job closes the receive right ... or exits, the receive right obtained by this routine will be send back to launchd(8) rather than being closed."* `man 5 launchd.plist` 의 `ResetAtClose`: false(기본)면 포트를 재활용해 클라이언트는 잡의 재시작을 모른다, true 면 dead-name 을 받는다.

이 맥의 실제 항목 (`launchctl print system/com.apple.notifyd`, 발췌):

```text
	program = /usr/sbin/notifyd
	endpoints = {
		"com.apple.system.notification_center" = {
			port = 0xca03
			active = 1
			managed = 1
```

`demo_bootstrap.c` 실측:

<!-- code: demo_bootstrap.c -->

```text
pid 79340, bootstrap_port = 0x813 (this task's send right to its launchd bootstrap namespace)
1) look_up com.apple.system.notification_center -> 0, name 0x1d03, type 0x10000 (SEND=0x10000)
2) look_up unknown name -> 1102 (Unknown service name)
3) check_in com.example.os-a14.checkin.79340 -> 0 (Success), name 0x1d0f type 0x20000 (RECEIVE=0x20000)
4) bootstrap_register com.example.os-a14.79340 -> 0 (Success)
    [child 79374] bootstrap_look_up -> 0 (Success), send right name 0xb03
   child exit 0; parent received kr=0x0 hello from pid 79374
   after destroying the receive right:
    [child 79394] bootstrap_look_up -> 1102 (Unknown service name), send right name 0x0
```

1)·2) 는 문서대로다. 3)·4) 는 **문서와 다르다**: man 페이지는 plist 에 없는 이름의 `check_in` 이 `BOOTSTRAP_UNKNOWN_SERVICE` 라고 하고, `xpc(3)` 는 *"Dynamic service registrations (ala bootstrap_register() ...) are also purposefully disallowed."* 라고 적는데, 이 맥(macOS 27.2, `sw_vers` ReleaseType 이 Internal 인 빌드, 터미널 세션)에서는 둘 다 성공했고 forked 자식이 그 이름으로 찾아 보냈다. receive right 를 부수면 이름이 사라진다. 일반 배포 빌드나 샌드박스 앱에서도 그런지는 확인하지 않았다 — 그래서 체크 테스트는 이 경로를 쓰지 않고 fork 특수 포트 트릭을 쓴다. "launchd 잡 없이 서비스 이름을 만들기" 는 제품에서 기대하면 안 되는 동작으로 본다.

**XPC.**

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">XPC 층 구조 (공개 API 기준, 내부는 비공개)</text>
  <rect x="10" y="36" width="350" height="44" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="20" y="56" fill="currentColor" font-weight="bold">앱: xpc_dictionary {op, a, b}</text>
  <text x="20" y="72" fill="currentColor">값 = int64, string, data, fd, shmem, endpoint ...</text>
  <rect x="10" y="88" width="350" height="62" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="20" y="108" fill="currentColor" font-weight="bold">libxpc: xpc_connection</text>
  <text x="20" y="126" fill="currentColor">직렬화, 응답 짝짓기, 이벤트 핸들러 = GCD 큐</text>
  <text x="20" y="144" fill="currentColor">오류 = INTERRUPTED / INVALID 객체</text>
  <rect x="10" y="158" width="350" height="44" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="20" y="178" fill="currentColor" font-weight="bold">Mach 메시지</text>
  <text x="20" y="194" fill="currentColor">포트 권리, OOL, send-once 응답</text>
  <rect x="10" y="210" width="350" height="40" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="20" y="235" fill="currentColor" font-weight="bold">XNU IPC: 포트 객체, 메시지 큐</text>
  <rect x="400" y="36" width="330" height="214" rx="4" stroke="currentColor" fill="none"/>
  <text x="410" y="58" fill="currentColor" font-weight="bold">연결 만들기 3 가지</text>
  <text x="410" y="82" fill="currentColor" font-family="monospace">xpc_connection_create_mach_service</text>
  <text x="410" y="100" fill="currentColor">→ launchd 이름 (MachServices)</text>
  <text x="410" y="124" fill="currentColor" font-family="monospace">xpc_connection_create("bundle.id")</text>
  <text x="410" y="142" fill="currentColor">→ 앱 번들 안 XPC 서비스</text>
  <text x="410" y="166" fill="currentColor" font-family="monospace">xpc_connection_create(NULL, q)</text>
  <text x="410" y="184" fill="currentColor">→ 익명 리스너 (이 레슨)</text>
  <text x="410" y="202" fill="currentColor">→ xpc_endpoint 로 상대에게 전달</text>
  <text x="410" y="226" fill="currentColor">이름이 없으니 재발견 불가:</text>
  <text x="410" y="244" fill="currentColor">리스너가 죽으면 INVALID</text>
</svg>
```

익명 리스너는 이름 서비스에 등록되지 않는다. 엔드포인트(`xpc_endpoint_create`)를 XPC 메시지에 실어 다른 프로세스에 넘기는 것이 원래 용도이고, 이 레슨은 같은 프로세스에서 `xpc_connection_create_from_endpoint` 로 연결한다. 헤더 `xpc/connection.h` 209–228 행: *"Since the connection yielded by this API is not associated with a name (and therefore is not rediscoverable), this connection will receive XPC_ERROR_CONNECTION_INVALID if the listening side crashes, exits or cancels the listener connection."* — C3 의 마지막 검사가 정확히 이것이다.

오류 두 종류(헤더 34·53 행): `XPC_ERROR_CONNECTION_INTERRUPTED` 는 "상대가 죽었지만 이름으로 다시 연결될 수 있다"(launchd 서비스, 재시도 가능), `XPC_ERROR_CONNECTION_INVALID` 는 "이 연결은 끝났다". 익명 연결에서는 INTERRUPTED 가 올 일이 없다. `xpc_connection_send_message_with_reply_sync` 문서(548–582 행)는 우선순위 역전 회피를 지원하니 `send_message_with_reply` + 세마포어 조합 대신 쓰라고 하고, **타깃 큐 계층 안에서 부르면 교착** 할 수 있다고 경고한다.

C3 실측 줄: `peers=4 peers_gone=4 msgs=253` — 연결 4 개(메인 1 + 스레드 3), 취소된 피어 4 개 모두 서버 쪽에서 INVALID 를 받았다.

XPC 객체를 만들 때 이 태스크의 Mach 이름이 어떻게 늘어나는지 (`demo_xpc_ports.c`, 실측):

<!-- code: demo_xpc_ports.c -->

```text
start                              names=11  receive= 6 send= 6 send-once=0 dead=0 port-set=0
listener created (inactive)        names=12  receive= 7 send= 7 send-once=0 dead=0 port-set=0
listener activated                 names=12  receive= 7 send= 7 send-once=0 dead=0 port-set=0
endpoint created                   names=12  receive= 7 send= 7 send-once=0 dead=0 port-set=0
client connection activated        names=14  receive= 9 send= 8 send-once=0 dead=0 port-set=0
after 1 sync call                  names=19  receive=10 send=14 send-once=0 dead=0 port-set=0
client: <connection: 0x1050f6be0> { name = (anonymous), listener = false, pid = 87481, euid = 501, egid = 20, asid = 100022 }
```

리스너 하나 = receive+send 한 쌍, 연결과 첫 호출에 포트가 더 생긴다. 어떤 포트가 무슨 역할인지는 libxpc 가 비공개라 말할 수 없다 — "XPC 연결 = Mach 포트 몇 개" 까지만 관찰로 말한다. 연결 설명에 pid/euid/asid 가 있는 것은 커널이 메시지마다 붙이는 audit token 덕이다(연습 3 에서 직접 꺼낸다).

**리눅스 Binder 와 비교.** Binder 쪽은 Linux 6.18.55 소스(VM `/var/lab/src/linux-6.18.55/`)에서 해당 줄을 읽은 것만 적는다. 실행해 보지는 않았다.

| | Mach (macOS) | Binder (Linux/Android) |
|---|---|---|
| 핸들 | 포트 이름 = 태스크별 권리 표 인덱스 | handle = 프로세스별 참조 번호 (`BINDER_TYPE_HANDLE`, uapi `binder.h` 34 행), 객체 자체는 `BINDER_TYPE_BINDER`(32 행) |
| 작은 메시지 복사 | 송신 → 커널 메시지 → 수신 버퍼 (2 번) | 송신자 사용자 버퍼 → **수신자 프로세스의 binder 버퍼**로 바로 1 번 (`binder.c` 3502·3733 행 `binder_alloc_copy_user_to_buffer(&target_proc->alloc, ...)`) |
| 왜 1 번이 되나 | — | 수신자가 `/dev/binder` 를 mmap 해 둔 영역에 커널이 직접 쓴다. 수신자 쪽 매핑은 쓰기 금지 (`binder.c` 100 행 `FORBIDDEN_MMAP_FLAGS (VM_WRITE)`, `binder_mmap` 6040 행) |
| 큰 데이터 | OOL 가상 복사 (COW, 페이지 단위) | 버퍼 크기 한도가 있어 큰 데이터는 ashmem/memfd fd 를 넘기는 것이 관례 (문서 기반, 미검증) |
| 객체 전달 | 포트 디스크립터 (권리 이동, 커널이 이름 번역) | 객체 참조 (커널이 node ↔ handle 번역, 참조 카운트) |
| 상대 사망 | dead-name / no-senders / send-once 알림 | `BC_REQUEST_DEATH_NOTIFICATION`(546 행) → `BR_DEAD_BINDER`(444 행) |
| 이름 서비스 | launchd (bootstrap) | servicemanager (context manager, handle 0) — 문서 기반 |

"단일 복사" 의 정확한 뜻은 **송신자에서 수신자로 데이터가 한 번만 복사된다**는 것이지 0 번이 아니다. 6.18 트리에는 C 구현 옆에 Rust 구현(`drivers/android/binder/*.rs`)도 있다(디렉터리 확인).

### 지연 실측 — demo_bench.c

<!-- code: demo_bench.c -->

측정 환경: Apple M6 (`sysctl -n machdep.cpu.brand_string`), macOS 27.2, Apple clang 21.0.0, `hw.pagesize` 16384. 스레드 2 개 핑퐁, 16 B, 20000 회(+2000 워밍업) × 3 회. **공유 머신이 바빴다** — 1회차 직전 `uptime` load averages `13.13 12.71 10.22`, 2회차 `22.11 15.30 11.46`. p99 는 다른 작업에 크게 흔들리므로 **min 과 p50 의 비율**만 믿는다.

| 방식 | 1회차 min / p50 (ns, run1–3) | 2회차 min / p50 (ns, run1–3) | 왕복당 syscall |
|---|---|---|---|
| mach-2call | 2250/5750, 2458/5125, 2292/5166 | 2000/4167, 2000/3708, 2041/6459 | 4 |
| mach-1call (SEND\|RCV 결합) | 1917/4583, 2041/4375, 2042/4458 | 1667/3166, 1792/3375, 1791/3959 | 2 |
| pipe | 2208/4500, 2333/4542, 2291/5792 | 1959/4292, 2000/3958, 2000/3500 | 4 |
| Unix socket | 2584/6458, 2458/5417, 2541/5583 | 2125/3375, 2166/4834, 2167/3583 | 4 |
| XPC (sync reply) | 9042/21084, 8584/21084, 8542/22667 | 8208/29917, 6000/26209, 5542/31458 | (비공개) |

읽는 법:
- 원시 mach_msg 는 pipe·소켓과 **같은 급**이다. "Mach 는 느리다" 도 "빠르다" 도 아니다. 이득은 `SEND|RCV` 를 한 번에 하는 결합 호출(min 기준 약 10–15 % 빠름)에서 나온다: 클라이언트는 보내고 바로 자기 응답 포트에서 잠들고, 서버는 "응답 + 다음 요청 기다림" 을 한 번에 한다(MIG 서버 루프가 쓰는 모양).
- XPC 는 min 기준 약 3–4 배, p50 기준 4–7 배 느리다. 딕셔너리 생성·직렬화·GCD 큐 디스패치·응답 짝짓기 비용이다. 대신 얻는 것: 타입 있는 메시지, 연결 수명 관리, 우선순위 전파, launchd 기동.

inline vs OOL (한 스레드가 보내고 바로 받기, µs/메시지):

| 크기 | 1회차 inline | OOL | OOL + 수신측 전부 읽기 | memcpy | 2회차 inline | OOL | OOL+읽기 | memcpy |
|---|---|---|---|---|---|---|---|---|
| 4 KiB | 0.90 | 4.36 | 4.86 | 0.04 | 1.01 | 30.46 | 7.04 | 0.06 |
| 64 KiB | 20.13 | 2.82 | 7.55 | 0.98 | 19.67 | 3.01 | 12.96 | 0.64 |
| 1 MiB | 305.96 | 3.49 | 88.36 | 17.15 | 268.67 | 3.51 | 73.66 | 16.32 |
| 8 MiB | 2075.31 | 9.42 | 727.20 | 155.06 | 2308.23 | 14.54 | 840.18 | 333.31 |

- 작은 메시지(4 KiB, 페이지 16 KiB 보다 작음)는 inline 이 OOL 보다 빠르다. 2회차 4 KiB OOL 30 µs 는 튄 값으로 본다(같은 줄의 OOL+읽기가 7 µs).
- OOL 자체는 크기에 거의 무관(1 MiB 3.5 µs) — 데이터를 옮기지 않고 매핑만 만든다.
- 수신자가 실제로 **읽으면** 비용이 돌아온다(1 MiB 74–88 µs, memcpy 의 약 4–5 배): 처음 만지는 페이지마다 폴트를 처리한다. "가상 복사 = 공짜" 가 아니라 "쓰지 않는 부분은 공짜" 다.
- inline 1 MiB(270–306 µs)는 memcpy(17 µs)의 15 배 이상이다. 커널 메시지 버퍼 할당 + 복사 2 번 + 수신 버퍼 접근. 큰 데이터를 inline 으로 보내지 말라는 숫자다. inline 상한도 있다: 16 MiB 는 됐고 64 MiB 는 `MACH_SEND_TOO_LARGE`(0x1000000e) — 탐색 실험에서 확인.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so      # 58/58 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_as && ./so_as
cc -std=c11 -g -O1 -fsanitize=thread solution.c -o so_ts && ./so_ts
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bootstrap.c -o demo_bootstrap && ./demo_bootstrap
cc -std=c11 -Wall -Wextra -Werror -O2 demo_xpc_ports.c -o demo_xpc_ports && ./demo_xpc_ports
```

실제 결과: 일반 빌드 6 회(5 회는 cwd = `/`) 모두 `58/58 passed`, 한 번에 0.20–0.33 초. ASan+UBSan `58/58 passed`, 보고 0 건. TSan 3 회 `58/58 passed`, 경고 0 건.

코드에서 볼 곳:
- `server_handle_one` 의 형식 검사 4 가지 — 받은 메시지는 **신뢰할 수 없는 입력**이다. 크기·id·비트를 보고, 숫자 연산은 `uint64_t` 로 해서 상대가 준 값으로 부호 있는 오버플로(UB)가 나지 않게 했다.
- `rpc_call` 의 타임아웃 분기 — "새것 먼저, 옛것 파괴".
- `spawn_child_server` — `task_set_special_port` 복원이 fork 성공·실패와 무관하게 먼저 실행된다.
- XPC 서버 문맥 `g_xs` 가 전역인 이유: 블록이 포인터를 붙잡고, 이벤트는 비동기로 늦게 올 수 있다. 스택에 두면 함수가 끝난 뒤 핸들러가 해제된 스택을 만진다(연습 5 의 주제).

## 7. 자주 하는 실수

1. **수신 버퍼에 트레일러 자리를 안 둔다** → `MACH_RCV_TOO_LARGE`, 그리고 그 메시지는 (실려 온 권리와 함께) 사라진다. → `msg + mach_msg_max_trailer_t` union.
2. **`MACH_MSGH_BITS_COMPLEX` 를 빼먹는다** — 헤더 주석: 비트가 없으면 커널은 본문에 권리·OOL 이 없다고 가정하고, 디스크립터처럼 보이는 바이트는 그냥 데이터다(*"the behavior is undefined"*). 권리가 안 넘어간다.
3. **dead-name 알림 뒤 `deallocate` 를 한 번만** → 이름 누수(uref 2). B4 테스트가 잡는다.
4. **받은 send-once 를 쓰지도 버리지도 않는다** → 클라이언트는 무한 대기, 서버는 이름 누수. → 답하거나, `mach_port_deallocate`/`mach_msg_destroy` 로 버려 SEND_ONCE 알림을 보낸다.
5. **타임아웃 뒤 같은 응답 포트를 계속 쓴다** → 다음 호출이 이전 호출의 늦은 응답을 받는다. → 포트 교체(이 레슨) 또는 상관 ID 로 걸러 내기(OS-A13).
6. **받은 OOL 을 `vm_deallocate` 안 한다** → 메시지마다 VM 영역이 쌓인다. 형식 불량이면 `mach_msg_destroy` 가 대신 한다.
7. **디스포지션 혼동** — `MAKE_SEND` 는 receive right 가 있어야 하고, `COPY_SEND` 는 send right 가 있어야 하며, `MOVE_SEND` 는 내 것을 잃는다. 자식이 `MAKE_SEND` 로 자기 포트를, 부모는 `COPY_SEND` 로 서버를 가리키는 이유.
8. **포트 이름(숫자)을 다른 프로세스에 정수로 보낸다** — 그쪽 이름 공간에선 의미가 없다. 권리는 반드시 디스크립터로.
9. **fork 한 자식에서 XPC/libdispatch 를 쓴다** — fork 안전하지 않다. 자식에서는 `mach_msg` 와 `_exit` 만. 새 프로그램이면 `posix_spawn` + `posix_spawnattr_setspecialport_np`.
10. **XPC 동기 호출을 자기 연결의 타깃 큐에서 부른다** → 응답을 처리할 큐가 나를 기다리는 교착(헤더 경고).

## 8. 면접 Q&A

<details><summary>Q1. receive right, send right, send-once right 의 차이는? 왜 receive right 는 하나뿐인가?</summary>

receive right = 그 포트의 큐에서 메시지를 꺼낼 권리. 포트당 정확히 하나, 한 태스크만 가진다 — "이 포트로 온 메시지는 누가 받나" 가 항상 하나로 정해져야 메시지가 갈 곳이 명확하다(그 대신 큐를 여러 스레드가 함께 기다리는 것은 된다). send right = 보낼 권리, 여러 태스크가 가질 수 있고 태스크 안에서는 이름 하나에 uref 로 셈한다. send-once = 정확히 한 번 보낼 수 있는 권리, 응답용. **꼬리: 같은 포트가 두 태스크에서 같은 이름인가?** 아니다. 이름은 태스크별 표 인덱스다. B1 에서 같은 태스크 안으로 보내면 같은 이름 + uref 증가, C1 에서 자식 포트는 부모 쪽에서 다른 숫자였다. **꼬리: fd 와 무엇이 같나?** 둘 다 프로세스별 번호 → 커널 객체. 차이는 fd 는 상속·경로로도 얻지만 포트 권리는 메시지로만 얻는다는 것(SCM_RIGHTS 가 OS-M16 에서 하는 일이 Mach 에선 기본 동작).
</details>

<details><summary>Q2. launchd 는 서비스 이름을 어떻게 포트에 연결하나? 서비스가 아직 안 떠 있으면?</summary>

plist 의 `MachServices` 를 읽어 launchd 가 포트를 만들고 receive right 를 **자기가 보관**한 채 이름을 광고한다. 클라이언트는 `bootstrap_look_up`(또는 `xpc_connection_create_mach_service`)으로 send right 를 받아 바로 보낸다. 메시지가 큐에 쌓이면 launchd 가 잡을 띄우고, 잡은 `bootstrap_check_in` 으로 receive right 를 넘겨받아 쌓인 메시지를 읽는다 — 그래서 on-demand 가 클라이언트에게 투명하다. **꼬리: 잡이 죽으면?** receive right 가 launchd 로 돌아가 같은 포트가 계속 광고된다(기본). `ResetAtClose` 면 클라이언트가 dead-name 을 받는다. **꼬리: 아무 프로세스나 이름을 등록할 수 있나?** XPC man 페이지는 동적 등록을 의도적으로 막는다고 한다. 이 맥의 실측은 폐기 API 가 터미널 세션에서 여전히 성공했지만(5. 풀이), 제품에선 plist 로만 이름을 만든다고 답하는 게 맞다. 이름 = launchd 정책이 통제하는 지점이기도 하다.
</details>

<details><summary>Q3. 상대 프로세스가 죽은 것을 어떻게 아나? pid 를 저장해 두고 kill(pid,0) 하면 안 되나?</summary>

Mach 에선 커널이 알림을 보낸다: 클라이언트는 서버 send right 에 dead-name, 서버는 세션 포트에 no-senders, 진행 중인 요청은 send-once 알림. 모두 **포트가 신원**이라 pid 재사용과 무관하고 폴링이 없다. `kill(pid,0)` 은 pid 가 재사용되면 엉뚱한 프로세스를 살아 있다고 본다(OS-A12 의 주의점). **꼬리: 실측으로 얼마나 빨리 오나?** C2 에서 `SIGKILL` 직후 3 초 시한 안에 왔다(시간은 체크하지 않음). **꼬리: 리눅스에서 같은 것은?** Unix 소켓 EOF/`POLLHUP`, pidfd(OS-M18), Binder death notification(`BR_DEAD_BINDER`).
</details>

<details><summary>Q4. Mach 메시지는 데이터를 몇 번 복사하나? Binder 의 "단일 복사" 는 어떻게 가능한가?</summary>

inline: 송신 사용자 → 커널 메시지 버퍼 → 수신 사용자, 2 번. OOL: 데이터 복사 대신 COW 매핑(실측 1 MiB 3.5 µs, 단 수신자가 읽으면 폴트 비용). Binder: 수신 프로세스가 `/dev/binder` 를 mmap 해 둔 버퍼(사용자에겐 읽기 전용, `FORBIDDEN_MMAP_FLAGS`)에 커널이 송신자 사용자 메모리에서 **바로** 쓴다(`binder_alloc_copy_user_to_buffer(&target_proc->alloc, ...)`, binder.c 3733 행). 그래서 1 번. **꼬리: OOL 이 항상 이득인가?** 아니다. 4 KiB(페이지보다 작음)에선 inline 0.9 µs vs OOL 4.4 µs 로 inline 이 이겼다. 크기 경계는 측정으로 정한다.
</details>

<details><summary>Q5. 클라이언트 타임아웃을 넣었더니 가끔 엉뚱한 응답을 받는다. 원인과 해결은?</summary>

타임아웃 뒤에도 요청은 서버 큐에 있다. 서버가 나중에 같은 응답 포트로 답하면, 다음 호출이 그것을 읽는다. 해결 둘: (1) 응답 포트를 버리고 새로 만든다(MIG 방식, 이 레슨) — 늦은 응답은 서버에서 `MACH_SEND_INVALID_DEST` 가 되고 서버는 dead name 을 정리. (2) 요청 ID 로 짝짓고 모르는 응답은 버린다(OS-A13). **꼬리(staff): 이 설계가 틀리는 경우는?** 타임아웃이 "서버가 처리 안 했다" 를 뜻하지 않는다. 서버는 이미 일을 했을 수 있다(쓰기 연산이면 중복 실행 위험) → 요청에 멱등 키를 넣거나, 비멱등 연산은 "결과 모름" 상태를 호출자에게 그대로 올려야 한다. 그리고 응답 포트 교체는 타임아웃마다 포트 생성/파괴 비용 — 타임아웃이 잦은 경로면 seq 걸러 내기가 싸다.
</details>

<details><summary>Q6. Mach 메시지 서버에서 보안상 꼭 해야 하는 검사는?</summary>

메시지는 신뢰할 수 없는 입력이다: `msgh_id`·`msgh_size`·COMPLEX 비트·디스크립터 개수와 타입·응답 권리 종류를 검사하고, 맞지 않으면 `mach_msg_destroy` 로 권리·메모리를 반납한다(안 하면 상대가 서버의 이름 공간을 채울 수 있다). 숫자는 오버플로가 UB 가 되지 않게. 보낸 사람 신원은 pid 가 아니라 커널이 붙이는 **audit token**(연습 3)으로 — pid 는 재사용되고, 확인과 사용 사이에 바뀔 수 있다. **꼬리: XPC 에선?** `xpc_connection_get_pid` 같은 값이 보이지만, 권한 판단은 audit token 이나 코드 서명 요구 조건으로 하는 것이 권장된다고 알고 있다(SDK 에 `peer_requirement.h` 헤더가 있음을 확인, 사용법은 검증 안 함).
</details>

<details><summary>Q7. 큐가 가득 차면 어떻게 되나? 응답이 막히면 교착이 생기지 않나?</summary>

포트 큐 한도 기본값은 5(`MACH_PORT_QLIMIT_DEFAULT`, `port.h` 276 행). 넘치면 송신자가 막히거나 `MACH_SEND_TIMEOUT` 이면 `MACH_SEND_TIMED_OUT` — 자연스러운 역압이다(연습 2 실측: 5 개 수락 뒤 6 번째 실패). 그런데 send-once 로 보내는 메시지는 꽉 찬 큐에도 들어갔다(실측 msgcount 6 > 5). 응답 경로가 큐 한도에 막히면 "서버는 응답을 못 보내 멈추고, 클라이언트는 요청을 못 보내 멈추는" 교착이 생길 수 있으니 응답은 막히지 않게 한 설계로 이해한다. **꼬리: XPC 는?** 내부 큐잉은 비공개다. 대량 비동기 전송은 응답을 받으며 흐름을 조절하는 것이 안전하다(측정 안 함).
</details>

<details><summary>Q8. (staff) 이 Mach/XPC 설계를 임베디드 리눅스 장치로 옮겨야 한다면 무엇으로 바꾸겠나?</summary>

같은 성질별로 고른다. 권리 전달 → Unix 소켓 + `SCM_RIGHTS`(OS-M16), 신원 → `SO_PEERCRED`/`SO_PEERPIDFD`, 상대 사망 → 소켓 EOF + pidfd(OS-M18), 이름 서비스 → systemd 소켓 활성화(launchd on-demand 와 같은 생각) 또는 D-Bus 버스 이름, 대용량 → memfd + 공유 매핑(OS-A12), RPC 프레이밍 → OS-A13. Android 계열이면 Binder 가 Mach 에 가장 가깝다(객체 참조 + death notification). 제약이 큰 MCU(RTOS)라면 큐 핸들이 사실상 권리다: 핸들을 받은 태스크만 보낼 수 있고, 응답은 요청에 "응답 큐 핸들" 이나 태스크 알림(RTOS-M01)을 실어 같은 모양을 만든다. **꼬리: 무엇을 잃나?** 커널이 강제하는 권리 모델. 소켓 경로는 파일 권한으로, 공유 메모리는 매핑 권한으로 각각 따로 지켜야 한다.
</details>

<details><summary>Q9. 서버가 응답 없이 죽었는데 클라이언트가 30 초 타임아웃에서야 깨어난다. 무엇을 의심하나?</summary>

send-once 알림이나 dead-name 이 왔어야 할 상황이다. 의심 순서: (1) 서버가 응답 권리를 **다른 곳에 넘겨 둔 채** 살아 있다(작업 큐에 send-once 를 보관 — 권리가 살아 있으니 알림이 없다). (2) 클라이언트가 응답 포트에서 SEND_ONCE 알림(id 71)을 응답 형식 불량으로 버리고 다시 기다린다. (3) 응답을 같은 포트로 받는 여러 호출이 서로의 알림을 소비한다. (4) 서버가 죽은 게 아니라 큐가 가득 차 막혀 있다(`mach_port_get_attributes` 의 `mps_msgcount`). **꼬리: 어떻게 테스트에 넣나?** A4 처럼 OP_DROP 을 서버에 넣어 "처리 중 죽음" 을 결정적으로 만든다.
</details>

## 9. 추가 연습

각 답은 `practice.c` 에 있다.

1. **포트 셋.** 서비스 포트 둘을 포트 셋에 넣고 한 스레드가 셋에서 받는다. 메시지가 어느 서비스로 왔는지 어떻게 아나?

<details><summary>답</summary>

`mach_port_allocate(MACH_PORT_RIGHT_PORT_SET)` + `mach_port_insert_member` 두 번, 수신은 셋 이름으로. 받은 메시지의 `msgh_local_port` 가 실제 도착한 포트다(수신 시 local/remote 가 바뀌므로). 테스트: a 로 4 개, b 로 2 개 → 정확히 4/2, 이후 셋은 비어 타임아웃. epoll/kqueue(OS-M12) 의 Mach 판이다.
</details>

2. **큐 한도 역압.** 기본 한도에서 몇 개가 들어가나? 한도를 1 로 줄이면? send-once 는?

<details><summary>답</summary>

실측: `default qlimit=5, accepted=5, then 0x10000004 (MACH_SEND_TIMED_OUT); send-once → msgcount=6`. `mach_port_set_attributes(MACH_PORT_LIMITS_INFO, {qlimit=1})` 뒤에는 첫 번째만 들어간다. `MACH_SEND_TIMEOUT` + 0 ms 는 "막히면 즉시 실패" 다 — RTOS 큐의 `xQueueSend(..., 0)` 와 같은 모양(RTOS-B11).
</details>

3. **보낸 사람 신원.** 트레일러에 audit token 을 요청해 보낸 프로세스의 pid 를 얻는다. 자기 자신과 fork 한 자식 두 경우.

<details><summary>답</summary>

수신 옵션에 `MACH_RCV_TRAILER_TYPE(MACH_MSG_TRAILER_FORMAT_0) | MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AUDIT)`, 버퍼는 `mach_msg_audit_trailer_t` 크기만큼. `audit_token_to_pid`(`bsm/libbsm.h`) 는 libbsm 에 있어 `-lbsm` 이 필요하다(링크 실패로 확인) — 그래서 `practice.c` 첫 3 줄에 `/* BUILD: -lbsm */`. 실측: `me=66017 child=66024 audit pid=66024`. 커널이 채우는 값이라 보낸 쪽이 위조할 수 없다. OS-M16 의 `LOCAL_PEERPID`/`getpeereid` 와 같은 역할이지만, 메시지마다 붙는다는 점이 다르다.
</details>

4. **MIG 흉내.** `msgh_id` → 처리 함수 표, 응답 id = 요청 id + 100, 모르는 id 는 `MIG_BAD_ID`. libSystem 의 `mach_msg_server_once` 로 돌린다.

<details><summary>답</summary>

demux 함수는 `(in, out) → boolean_t`: 응답 헤더를 채우고(remote = 받은 send-once, `MACH_MSGH_BITS(REMOTE(in), 0)`), 표에 없으면 `mig_reply_error_t.RetCode = MIG_BAD_ID` 와 FALSE. `mach_msg_server_once` 가 받기 → demux → 응답 보내기를 해 준다. 요청을 미리 큐에 넣어 두어 이 함수가 막히지 않게 했다. 실측: `add → sum=42; unknown id → RetCode=-303 (MIG_BAD_ID=-303)`. 실제 MIG 는 `.defs` 파일에서 이 표와 직렬화 코드를 생성한다.
</details>

5. **XPC 비동기 응답 + 데드라인.** `send_message_with_reply` + 세마포어 시한. 시한이 지난 뒤 응답이 늦게 와도 해제된 메모리를 만지지 않게 하라.

<details><summary>답</summary>

호출 문맥 `call_t` 를 힙에 두고 참조 카운트 2(호출자 + 핸들러)로 시작한다. `late` 플래그를 `atomic_exchange` 로 먼저 세운 쪽이 이긴다: 핸들러가 먼저면 정상 응답, 호출자가 먼저면 시한 초과이고 핸들러는 나중에 와서 "늦음" 만 세고 해제한다. 스택 변수를 블록이 캡처하게 두면 함수가 돌아간 뒤 핸들러가 죽은 스택을 쓴다 — ASan 빌드로 확인하는 연습이다. 실측: 50 ms 시한 / 서버 300 ms 지연 → 시한 초과, 늦은 응답 1 건 처리, 연결은 계속 사용 가능. 헤더가 권하듯 단순 동기 대기라면 `_sync` 가 우선순위 역전 회피 면에서 낫고, 시한이 필요할 때만 이 모양을 쓴다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -lbsm -o pr && ./pr
```

실제 결과: `17/17 passed` (0.46 초). ASan+UBSan, TSan 빌드 모두 `17/17 passed`, 보고 0 건.

## 10. 핵심 정리

- 포트 = 커널 객체(큐), 이름 = 태스크별 번호, 권리 = receive(포트당 1) / send(여럿, uref) / send-once(1 회). 권리는 메시지로만 이동한다.
- 요청 = `(COPY_SEND, MAKE_SEND_ONCE)`, 응답 = `MOVE_SEND_ONCE`. 받으면 remote/local 이 바뀐다.
- send-once 를 쓰지 않고 버리면 커널이 id 71 알림 → 클라이언트는 무한 대기 대신 `MIG_SERVER_DIED`.
- 타임아웃 뒤엔 응답 포트를 교체한다(새것 먼저). 늦은 응답의 dead name 은 서버가 지운다.
- 복합 메시지 = COMPLEX 비트 + 디스크립터. OOL = 보낸 순간의 COW 스냅샷, 받은 쪽이 `vm_deallocate`. 작으면 inline, 크면 OOL — 경계는 측정으로.
- 사망 감지 = dead-name(72, uref 2 개 정리) / no-senders(70) / send-once(71). pid 재사용과 무관.
- launchd = 이름 → 포트(receive right 보관, on-demand). XPC = 딕셔너리 + 연결 수명 + 오류 객체(INTERRUPTED vs INVALID) 를 Mach 위에 얹은 층. 실측 RTT 는 원시 mach_msg ≈ pipe ≈ 소켓, XPC 는 그 3–7 배.
- Binder: 수신자 mmap 버퍼로 바로 쓰는 단일 복사 + 객체 참조 + `BR_DEAD_BINDER`.

## 참고자료

- SDK 헤더 (MacOSX26.4.sdk): `mach/message.h` (헤더 비트, 디스크립터, 옵션, 반환 코드), `mach/port.h` (권리 종류, `MACH_PORT_QLIMIT_DEFAULT`), `mach/notify.h` (알림 id·구조체), `mach/mig_errors.h`, `servers/bootstrap.h`, `xpc/connection.h`, `xpc/endpoint.h`, `spawn.h` (`posix_spawnattr_setspecialport_np`), `bsm/libbsm.h`
- 이 맥의 man 페이지: `man 3 bootstrap_check_in` (launch(3)), `man 3 xpc`, `man 3 xpc_connection_create`, `man 5 launchd.plist` (MachServices, ResetAtClose), `man 8 launchd`. `mach_msg`/`mach_port_*` man 페이지는 이 맥에 없다.
- Linux 6.18.55: `drivers/android/binder.c` (100, 3436, 3502, 3733, 6040 행), `drivers/android/binder_alloc.c` (1330 행), `include/uapi/linux/android/binder.h` (32, 34, 444, 546 행)
- Apple, *Daemons and Services Programming Guide* (launchd · XPC) — 개념 배경
- OSTEP 5장 (프로세스 API — fork), 13장 참고문헌 [R+89] R. Rashid et al., "Mach: A System Software Kernel", COMPCON 1989 (OSTEP 은 Mach 를 "Mac OS X 깊은 곳에 아직 산다" 고만 소개한다). 원 논문 M. Accetta et al., "Mach: A New Kernel Foundation for UNIX Development", USENIX Summer 1986 (기억에 의존한 서지 — 미확인)
- 관련 레슨: OS-B14 (pipe·소켓·shm 비교), OS-M03 (COW), OS-M12 (kqueue — 포트 셋과 비교), OS-M16 (SCM_RIGHTS·피어 자격), OS-M18 (pidfd), OS-A12 (공유 메모리 장애 감지), OS-A13 (미니 RPC — 상관 ID·타임아웃), RTOS-B11 (메시지 큐), RTOS-M01 (태스크 알림)
- 다음 단계: PLAT-M05 (멀티코어 메일박스 IPC), BMC-M04 (D-Bus)
