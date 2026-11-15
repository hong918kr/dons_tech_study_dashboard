---
id: OS-M16
time: 90분
updated: 2026-11-11
---
# OS-M16 Unix 소켓 심화: fd 전달(SCM_RIGHTS)과 피어 자격 확인

## 학습 목표

- `sendmsg`/`recvmsg` 의 보조 데이터(ancillary data, cmsg)에 `SCM_RIGHTS` 를 실어 **열린 fd 를 다른 프로세스로** 넘기고, 받은 쪽에서 그 fd 로 읽기·쓰기가 되는 것(파일 오프셋까지 공유)을 테스트로 보인다.
- control 버퍼를 **정렬된 union + `CMSG_SPACE`** 로 잡는 이유를 ABI(Darwin 12 B/4 B 정렬 vs Linux LP64 16 B/8 B 정렬) 수준에서 설명하고, `CMSG_LEN` 으로 잡은 버퍼가 리눅스에서 `MSG_CTRUNC` 를 내는 것을 모델 + VM 실측으로 재현한다.
- `MSG_CTRUNC` 를 검사하지 않거나 cmsg 를 무시하면 **fd 가 새거나 엉뚱한 fd(0 = stdin)를 쓰게** 되는 것을 fd 표 개수로 보인다 — macOS 와 Linux 의 동작이 다르다는 것까지.
- `getpeereid` · `LOCAL_PEERCRED`(`struct xucred`) · `LOCAL_PEERPID` 로 상대의 uid/gid/pid 를 읽고 `allow_peer(acl, uid)` 로 접근을 제어하는 "fd 브로커" 를 만든다. pid 로 인가하면 안 되는 이유를 실측으로 말한다.
- `SOCK_STREAM` 은 메시지를 합치고 `SOCK_DGRAM` 은 경계를 지킨다는 것을 테스트로 확인하고, 스트림 위에 길이 프레이밍을 얹는다.

## 1. 문제

"카메라 데몬이 만든 버퍼를 추론 프로세스에 복사 없이 넘기려면?", "root 데몬이 `/dev/i2c-1` 을 열어 일반 사용자 앱에게만 쓰게 하려면?", "이 소켓에 붙은 클라이언트가 누구인지 어떻게 믿나?" — 임베디드 리눅스 면접에서 IPC 꼬리질문이 깊어지면 결국 이 두 기능으로 온다. **fd 전달**과 **피어 자격 확인**은 Unix 도메인 소켓만 할 수 있다(TCP 로는 불가능). 실제 장비에서는 권한 분리 데몬(특권 부분만 root 로 남기고 fd 만 넘김), systemd 의 fd store(`sd_pid_notify_with_fds`), D-Bus 의 fd 타입(`h`, 내부는 SCM_RIGHTS — BMC-M04), Wayland/PipeWire 의 dma-buf fd 전달(카메라·디스플레이 프레임)이 전부 이 메커니즘이다.

OS-B14 에서 Unix 소켓을 "또 하나의 바이트 스트림" 으로 썼고, 그 선택 표에 "fd 전달 → socketpair/Unix 소켓 (OS-M16)" 이라고 적어 두었다. 이번에는 그 이유를 직접 만든다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">SCM_RIGHTS = 커널이 "열린 파일 객체" 의 참조를 다른 프로세스 fd 표에 새로 꽂아 준다</text>
  <rect x="20" y="40" width="170" height="130" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="32" y="60" fill="currentColor" font-weight="bold">프로세스 A (보냄)</text>
  <rect x="35" y="72" width="140" height="24" rx="3" class="f-mut" stroke="currentColor"/>
  <text x="44" y="89" fill="currentColor" font-family="monospace" font-size="12">fd 5  CLOEXEC=1</text>
  <text x="35" y="120" fill="currentColor" font-size="12">sendmsg(sock,</text>
  <text x="35" y="136" fill="currentColor" font-size="12">  data "F",</text>
  <text x="35" y="152" fill="currentColor" font-size="12">  cmsg RIGHTS [5])</text>
  <rect x="550" y="40" width="170" height="130" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="562" y="60" fill="currentColor" font-weight="bold">프로세스 B (받음)</text>
  <rect x="565" y="72" width="140" height="24" rx="3" class="f-mut" stroke="currentColor"/>
  <text x="574" y="89" fill="currentColor" font-family="monospace" font-size="12">fd 7  CLOEXEC=0</text>
  <text x="565" y="120" fill="currentColor" font-size="12">번호는 새로 (빈 칸)</text>
  <text x="565" y="136" fill="currentColor" font-size="12">fd 플래그 안 넘어옴</text>
  <text x="565" y="152" fill="currentColor" font-size="12">= dup() 처럼</text>
  <rect x="250" y="40" width="240" height="60" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="262" y="60" fill="currentColor" font-size="12">소켓 수신 큐 (전송 중, in-flight)</text>
  <text x="262" y="80" fill="currentColor" font-family="monospace" font-size="12">"F" + rights[ file* ]</text>
  <path d="M190 100 L248 75" stroke="currentColor" stroke-width="2"/>
  <path d="M490 75 L548 100" stroke="currentColor" stroke-width="2"/>
  <path d="M540 96 L550 100 L541 106" stroke="currentColor" fill="none" stroke-width="2"/>
  <rect x="250" y="190" width="240" height="64" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="262" y="210" fill="currentColor" font-weight="bold">열린 파일 설명 (open file description)</text>
  <text x="262" y="228" fill="currentColor" font-size="12">오프셋 6 → 13 (둘이 공유), O_RDWR</text>
  <text x="262" y="245" fill="currentColor" font-size="12">참조 수: A 의 fd + 큐 + B 의 fd</text>
  <path d="M105 96 L300 188" stroke="currentColor" stroke-width="2"/>
  <path d="M635 96 L440 188" stroke="currentColor" stroke-width="2"/>
  <rect x="300" y="276" width="140" height="28" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="312" y="295" fill="currentColor" font-size="12">vnode (unlink 돼도 OK)</text>
  <path d="M370 254 L370 274" stroke="currentColor" stroke-width="2"/>
  <text x="10" y="324" fill="currentColor" font-size="12">보낸 쪽이 직후 fd·소켓을 닫아도 큐가 참조를 쥐고 있어 받은 쪽은 정상으로 읽는다 (연습 3).</text>
</svg>
```

### Part A — send_fds / recv_fds: 열린 fd 를 넘긴다

- `send_fds(sock, fds, n, data, len)`: `struct msghdr` + 정렬된 control 버퍼에 `SOL_SOCKET`/`SCM_RIGHTS` cmsg 하나, fd 를 `n` 개(≤ `MAX_FDS` = 8) 담아 `sendmsg`. 데이터는 1 바이트 이상(스트림에 0 바이트 + fd 는 macOS 에서 `EMSGSIZE` — 실측).
- `recv_fds(sock, data, len, fds, max_fds, &n)`: `recvmsg` → `CMSG_FIRSTHDR`/`CMSG_NXTHDR` 로 순회 → fd 를 `memcpy` 로 꺼낸다 → 받은 fd 에 `FD_CLOEXEC` 를 켠다(macOS 에는 `MSG_CMSG_CLOEXEC` 가 없다 — SDK 헤더 확인).
- 테스트 4개: (A1) socketpair + fork, 부모가 **fork 뒤에** 연 파일을 넘기면 자식이 그 fd 로 읽고 부모의 오프셋도 같이 움직인다, (A2) pipe 쓰기 끝을 넘기고 부모는 자기 사본을 닫음 → 부모는 자식이 쓴 글과 EOF 를 본다, (A3) cmsg 하나에 fd 3개(파일·pipe·`/dev/null`), (A4) `/tmp/OS-M16_work/run.<pid>/` 의 이름 있는 소켓으로 connect 한 프로세스에게 넘기고 경로를 `unlink`.

### Part B — cmsg 버퍼 실수를 재현한다

- `cmsg_len_abi` / `cmsg_space_abi` / `cmsg_fit_count`: Darwin(헤더 12 B, 4 B 정렬)과 Linux LP64(헤더 16 B, 8 B 정렬) 두 ABI 의 `CMSG_LEN`/`CMSG_SPACE` 를 계산하는 모델. Darwin 값은 이 맥의 실제 매크로와 0..40 전부 일치해야 하고, Linux 값은 VM 실측(LEN(4)=20, SPACE(4)=24)과 일치해야 한다. "SCM_CREDENTIALS + SCM_RIGHTS 를 CMSG_LEN 합으로 잡은 버퍼" 가 리눅스에서는 두 번째 cmsg 를 잘라 먹고 맥에서는 멀쩡한 것 = **맥에서 테스트하고 보드에서 터지는 잠복 버그**.
- 진짜 커널로: 상한(1)보다 많은 fd(3)가 오면 `recv_fds` 는 `MSG_CTRUNC` 를 보고 `-1/EMSGSIZE`. 하네스의 "틀린 수신기" 3종(cmsg_len 을 믿음 / 버퍼를 `sizeof(int)` 로 / cmsg 무시)이 각각 **엉뚱한 fd**, **안 보이는 누수**를 만드는 것을 fd 표 개수로 확인.
- `send_record` / `recv_record`: 스트림은 `send` 3번이 `recv` 1번에 합쳐진다(10+20+30 → 60). 4 B 빅엔디언 길이 프레이밍으로 경계를 복원한다. DGRAM 은 경계 보존 + 잘리면 `MSG_TRUNC`, `SOCK_SEQPACKET` 은 macOS 에서 `EPROTONOSUPPORT`.

### Part C — 피어 자격과 접근 제어: fd 브로커

- `peer_identity(sock, &peer)`: `getpeereid`(uid, gid) + `getsockopt(SOL_LOCAL, LOCAL_PEERCRED)` 의 `struct xucred`(버전·길이·uid 일치 검사, 보조 그룹 수) + `LOCAL_PEERPID`.
- `allow_peer(acl, uid)`: 허용 목록. root 는 `allow_root` 로만, `(uid_t)-1` 은 항상 거부.
- `serve_one(lfd, acl, dev_fd, &who)`: accept → 자격 확인 → 허가면 `"OK"` + 장치 fd, 아니면 `"NO"`(fd 없음) → close. `/dev/i2c-1` 대신 내용이 `SENSOR-CAL v3` 인 임시 파일을 쓴다.
- 함정 재현: macOS 의 `LOCAL_PEERPID` 는 **connect 한 프로세스가 아니라 그 소켓을 마지막으로 쓴 프로세스** 를 돌려준다(실측). 리눅스 `SO_PEERCRED` 는 connect 시점 스냅숏(VM 실측) — 대조는 `demo_linux.c`.

## 2. Problem (English)

> **Pass open file descriptors and authenticate peers over Unix-domain sockets (macOS).**
>
> ```c
> ssize_t send_fds(int sock, const int *fds, int n, const void *data, size_t len); // n ≤ 8, len ≥ 1
> ssize_t recv_fds(int sock, void *data, size_t len, int *fds, int max_fds, int *nfds);
>     // returns data bytes (0 = EOF); received fds get FD_CLOEXEC;
>     // MSG_CTRUNC or an unexpected cmsg → close what you got, *nfds = 0, errno = EMSGSIZE, -1
> size_t cmsg_len_abi(const cmsg_abi_t *abi, size_t payload);     // CMSG_LEN for {hdr, align}
> size_t cmsg_space_abi(const cmsg_abi_t *abi, size_t payload);   // CMSG_SPACE for {hdr, align}
> int    cmsg_fit_count(const cmsg_abi_t *abi, size_t buflen, const size_t *payloads, int n);
> int    send_record(int sock, const void *p, uint32_t len);       // [u32 BE length][payload]
> int    recv_record(int sock, void *buf, uint32_t cap, uint32_t *len); // 1 rec, 0 EOF, -1 (EMSGSIZE/EPIPE)
> int    peer_identity(int sock, peer_t *out);                     // getpeereid + LOCAL_PEERCRED + LOCAL_PEERPID
> bool   allow_peer(const acl_t *acl, uid_t uid);
> int    serve_one(int lfd, const acl_t *acl, int dev_fd, peer_t *who); // 1 granted, 0 denied, -1 error
> ```
>
> A child that did not inherit a file must be able to read it through the received descriptor (the file offset is shared). Three descriptors in one control message must arrive as three distinct descriptors of the right types. A receiver whose buffer is too small must report truncation instead of handing out fds. A broker must give the device fd only to peers whose uid is in the allow-list.
>
> Follow-ups: Why `CMSG_SPACE` for the buffer but `CMSG_LEN` for `cmsg_len`? Why a `union` with `struct cmsghdr`? What happens to descriptors that do not fit — on macOS and on Linux? Why is the peer pid not an identity? How do Linux `SO_PEERCRED` and `SCM_CREDENTIALS` differ? Why does `SOCK_STREAM` need framing?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

macOS 전용이다(`LOCAL_PEERCRED`, `LOCAL_PEERPID`, `getpeereid`, `struct xucred` — `_DARWIN_C_SOURCE` 로 연다). 추가 빌드 플래그는 없다. 처음 실행 결과 (실제 출력, 0.2 초 이내 — 멈추지 않는다):

```text
FAIL starter.c:251  s == 1
FAIL starter.c:252  code == 0 && r == 7 && memcmp(reply, "payload", 7) == 0
FAIL starter.c:253  off == 13
FAIL starter.c:254  cloexec == 1
[A1] child read "" through the passed fd; parent offset 6 -> 6 (shared)
...
FAIL starter.c:677  i1 == 0 && p1.pid == C
FAIL starter.c:678  i2 == 0 && p2.pid == G && G != C
[C4] LOCAL_PEERPID: after connect by C=90164 -> 0; after grandchild G=90165 wrote -> 0
15/56 passed
```

통과하는 15개는 하네스 자체 점검(`align_up`, 스트림/DGRAM 원시 동작, SEQPACKET 미지원, 경로 삭제, 전체 fd 개수)과 "거부" 쪽 검사들이다. 하네스 구조:

| 장치 | 하는 일 | 왜 |
|---|---|---|
| `set_timeouts` | 모든 테스트 소켓에 `SO_RCVTIMEO`/`SO_SNDTIMEO` 300~1500 ms | 스텁이 아무것도 안 보내도 `recvmsg` 가 영원히 막히지 않는다 |
| 자식마다 `alarm(4)`, 부모는 `reap()` | `waitpid(WNOHANG)` 1 ms 폴링, 5 초 넘으면 `SIGKILL` | 틀린 코드는 FAIL 로 끝난다 |
| `fd_snap` / `fd_sweep` | 0..255 를 `fcntl(F_GETFD)` 로 스캔, 스냅숏 뒤 새로 생긴 fd 를 닫으며 센다 | "보이지 않게 설치된 fd" 를 숫자로 잡는다 |
| run 디렉터리 | `/tmp/OS-M16_work/run.<pid>/`, 파일은 `mkstemp` 직후 `unlink`, 소켓은 테스트 끝에 `unlink`, 마지막에 `rmdir` 를 CHECK | 남는 파일이 있으면 `rmdir` 이 실패한다 |

## 4. 힌트

<details>
<summary>힌트 1 — msghdr 의 두 갈래: 데이터(iov)와 보조 데이터(control)</summary>

`msg_iov` 에는 실제 데이터(최소 1 바이트), `msg_control`/`msg_controllen` 에는 cmsg 들이 줄지어 들어간다. cmsg 하나 = `struct cmsghdr { cmsg_len; cmsg_level; cmsg_type; }` + 데이터. fd 전달은 `level = SOL_SOCKET`, `type = SCM_RIGHTS`, 데이터 = `int` 배열. 헤더 다음 데이터 주소는 반드시 `CMSG_DATA(c)` 로 구한다(헤더 크기 + 정렬 패딩이 플랫폼마다 다르다).

</details>

<details>
<summary>힌트 2 — 버퍼는 SPACE, 헤더 값은 LEN, 메모리는 union</summary>

`CMSG_LEN(n)` = 정렬된 헤더 + n (끝 패딩 없음) → `cmsg_len` 필드에 넣는다. `CMSG_SPACE(n)` = 정렬된 헤더 + 정렬된 n → 버퍼 크기와 `msg_controllen` 에 쓴다. `char buf[...]` 만 선언하면 정렬이 1 바이트일 수 있으므로 `union { struct cmsghdr align; char buf[CMSG_SPACE(...)]; }` 로 잡는다. fd 는 `*(int *)CMSG_DATA(c)` 대신 `memcpy`.

</details>

<details>
<summary>힌트 3 — cmsg_len 은 "보낸 쪽이 주장한 길이" 다</summary>

버퍼가 작아 잘리면 커널은 `msg_flags` 에 `MSG_CTRUNC` 를 켠다. 이 맥에서는 잘린 cmsg 의 `cmsg_len` 이 **원래 길이(24)** 그대로 오고 `msg_controllen` 만 16 으로 줄어든다. 그러니 fd 개수를 `cmsg_len` 으로만 세면 버퍼 밖(지난 메시지의 흔적)을 읽는다. 실제로 들어온 끝(`msg_control + msg_controllen`)과 비교해서 작은 쪽만 믿고, `MSG_CTRUNC` 면 받은 것을 다 닫고 실패를 돌려준다.

</details>

<details>
<summary>힌트 4 — 스트림 프레이밍은 read_all 두 번</summary>

헤더 4 바이트를 `read_all` → 0 이면 레코드 경계에서의 깨끗한 EOF, 1~3 이면 레코드 중간 EOF(`EPIPE`). 길이가 `cap` 보다 크면 읽지 말고 `EMSGSIZE` — 상대가 보낸 길이를 그대로 `malloc`/`read` 에 쓰면 DoS 가 된다.

</details>

<details>
<summary>힌트 5 — 자격은 getpeereid 가 기본, xucred 는 버전부터</summary>

`getpeereid` 는 STREAM 소켓 전용(DGRAM 은 `EINVAL`). `LOCAL_PEERCRED` 는 `getsockopt(sock, SOL_LOCAL, LOCAL_PEERCRED, &xucred, &len)` — `cr_version == XUCRED_VERSION` 과 `len == sizeof` 를 먼저 본다. 인가 결정은 **uid** 로 한다. pid 는 기록(로그)용이다 — 이유는 Part C 풀이.

</details>

## 5. 풀이

### Part A — send_fds

```c
union { struct cmsghdr align; char buf[CMSG_SPACE(MAX_FDS * sizeof(int))]; } ctl;   /* 정렬 보장 */
memset(&ctl, 0, sizeof ctl);
struct iovec iov = { (void *)data, len };
msg.msg_iov = &iov; msg.msg_iovlen = 1;
msg.msg_control = ctl.buf;
msg.msg_controllen = CMSG_SPACE(n * sizeof(int));     /* 버퍼에서 차지하는 자리 */
struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS;
c->cmsg_len = CMSG_LEN(n * sizeof(int));              /* 헤더 + 데이터, 끝 패딩 없음 */
memcpy(CMSG_DATA(c), fds, n * sizeof(int));
```

- `msg_controllen` 을 "버퍼 전체(fd 8개 자리)" 로 주면 어떻게 될까? 뒤에 0 으로 채워진 자리가 "길이 0 인 cmsg" 로 해석되어 `sendmsg` 가 `EINVAL` 이다. 그래서 `CMSG_SPACE(n)` 으로 딱 맞춘다(`CMSG_LEN(n)` 도 받아 준다 — 마지막 cmsg 의 끝 패딩은 선택). 보내는 fd 가 닫힌 번호면 `EBADF`. `demo_cmsg_bugs.c` 3번 실측:

```text
3) sendmsg facts:
  msg_controllen = whole buffer  (44 B): sendmsg=-1 errno=22
  msg_controllen = CMSG_SPACE(4) (16 B): sendmsg=1 errno=0
  msg_controllen = CMSG_LEN(4)   (16 B): sendmsg=1 errno=0
  closed fd number 1000: sendmsg=-1 errno=9 (Bad file descriptor)
  STREAM, 0 data bytes + fd: sendmsg=-1 errno=40 (Message too long)
```

  리눅스도 같은 실험(VM, 1회성 스크래치 프로그램)에서 버퍼 전체 48 B → `EINVAL`, SPACE 24 B·LEN 20 B → 성공이었다.
- 커널은 fd **번호** 가 아니라 그 번호가 가리키는 **열린 파일 객체** 의 참조를 큐에 넣는다. 그래서 받은 쪽 번호는 달라지고(A3: 서로 다른 세 번호), 오프셋은 공유된다(A1).

### Part A — recv_fds 와 실행 결과

핵심은 "주장한 길이" 와 "실제로 들어온 길이" 를 따로 보는 것이다:

```c
const unsigned char *end = (const unsigned char *)msg.msg_control + msg.msg_controllen;
for (struct cmsghdr *c = CMSG_FIRSTHDR(&msg); c != NULL; c = CMSG_NXTHDR(&msg, c)) {
    if (c->cmsg_len < CMSG_LEN(0)) { bad = true; break; }
    if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS) { bad = true; continue; }
    size_t claimed = c->cmsg_len - CMSG_LEN(0);            /* 보낸 쪽 주장 */
    size_t avail   = (size_t)(end - CMSG_DATA(c));          /* 버퍼에 실제로 들어온 것 */
    size_t bytes   = claimed < avail ? claimed : avail;
    ...  /* int 단위로 memcpy, max_fds 를 넘는 것은 close 하고 bad */
}
if ((msg.msg_flags & MSG_CTRUNC) || bad) { /* 받은 fd 전부 close */ errno = EMSGSIZE; return -1; }
for (...) fcntl(fds[i], F_SETFD, FD_CLOEXEC);
```

`FD_CLOEXEC` 를 따로 켜는 이유: fd 플래그는 프로세스별이라 **전달되지 않는다**(macOS `man 4 unix`: "Per-process descriptor flags, set with fcntl(2), are not passed to a receiver", 연습 3 에서 보낸 쪽 1 → 받은 쪽 0 실측). 받은 fd 가 나중에 `exec` 되는 자식에게 새지 않게 하려면 켜야 한다. 리눅스는 `recvmsg(..., MSG_CMSG_CLOEXEC)` 로 원자적으로 켤 수 있다(VM 실측 `FD_CLOEXEC=1`). 맥에는 그 플래그가 없어서 `recvmsg` 와 `fcntl` 사이에 다른 스레드가 `fork+exec` 하면 샐 수 있다 — 멀티스레드 데몬이라면 `posix_spawn` 에 `POSIX_SPAWN_CLOEXEC_DEFAULT`(Apple 확장)를 쓰는 식으로 막는다.

```text
$ ./solution      (0.2 초 이내, 5회 연속 + cwd=/ 에서 1회 모두 56/56)
[A1] child read "payload" through the passed fd; parent offset 6 -> 13 (shared)
[B1] linux-lp64: creds+fd buffer sum-of-LEN 48 B fits 1/2, sum-of-SPACE 56 B fits 2/2; darwin sum-of-LEN 40 B fits 2/2
[B2] fd-table delta on macOS: truncated(3 into 1)=2 naive-trust-len=3 (fds 7,0,0) sizeof-int-buf=1 ignored-cmsg=1 correct=0
[B4] stream recvmsg sizes: 6 4 0 (fd arrived with the call containing 'G')
[C2] server sees peer uid=501 gid=20 pid=90193 (child 90193) ngroups=15
[C4] LOCAL_PEERPID: after connect by C=90196 -> 90196; after grandchild G=90197 wrote -> 90197
56/56 passed
```

A1 의 `6 -> 13`: 부모가 `lseek(f, 6)` 한 뒤 넘겼고 자식이 7 바이트(`payload`)를 읽었다. 부모의 오프셋도 13 이 됐다 — 같은 열린 파일 설명이기 때문이다(`dup` 과 같고, `open` 을 두 번 한 것과 다르다). A2 는 "부모가 자기 쓰기 끝을 닫으면 남은 writer 는 자식의 받은 사본뿐" 이라 부모가 `via pipe\n` 다음 EOF 를 정확히 받는다(OS-B02 의 EOF 규칙).

### Part B — CMSG_LEN vs CMSG_SPACE: ABI 가 답을 바꾼다

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">control 버퍼 배치 (칸 하나 = 4 B) — 같은 코드, 다른 ABI</text>
  <text x="10" y="52" fill="currentColor" font-size="12">Darwin, fd 1개</text>
  <g stroke="currentColor">
    <rect x="130" y="38" width="40" height="22" class="f-acc"/><rect x="170" y="38" width="40" height="22" class="f-acc"/>
    <rect x="210" y="38" width="40" height="22" class="f-acc"/><rect x="250" y="38" width="40" height="22" class="f-ok"/>
  </g>
  <text x="300" y="54" fill="currentColor" font-size="12">len·level·type(12) + fd → LEN 16 = SPACE 16</text>
  <text x="10" y="94" fill="currentColor" font-size="12">Linux LP64, fd 1개</text>
  <g stroke="currentColor">
    <rect x="130" y="80" width="80" height="22" class="f-acc"/><rect x="210" y="80" width="40" height="22" class="f-acc"/>
    <rect x="250" y="80" width="40" height="22" class="f-acc"/><rect x="290" y="80" width="40" height="22" class="f-ok"/>
    <rect x="330" y="80" width="40" height="22" class="f-mut"/>
  </g>
  <text x="380" y="96" fill="currentColor" font-size="12">hdr 16 + fd + 패딩 → LEN 20, SPACE 24</text>
  <text x="10" y="140" fill="currentColor" font-weight="bold">Linux: SCM_CREDENTIALS(ucred 12 B) 다음 SCM_RIGHTS(fd 1개)</text>
  <g stroke="currentColor">
    <rect x="20" y="152" width="64" height="22" class="f-acc"/><rect x="84" y="152" width="48" height="22" class="f-warn"/>
    <rect x="132" y="152" width="16" height="22" class="f-mut"/>
    <rect x="148" y="152" width="64" height="22" class="f-acc"/><rect x="212" y="152" width="16" height="22" class="f-ok"/>
    <rect x="228" y="152" width="16" height="22" class="f-mut"/>
  </g>
  <text x="26" y="168" fill="currentColor" font-size="11">hdr 16</text>
  <text x="88" y="168" fill="currentColor" font-size="11">ucred</text>
  <text x="152" y="168" fill="currentColor" font-size="11">hdr 16</text>
  <text x="260" y="168" fill="currentColor" font-size="12">필요한 버퍼 = SPACE 32 + SPACE 24 = 56 B</text>
  <line x1="212" y1="186" x2="212" y2="200" stroke="currentColor"/>
  <line x1="20" y1="193" x2="212" y2="193" stroke="currentColor"/>
  <text x="20" y="214" fill="currentColor" font-size="12">CMSG_LEN 합 = 28 + 20 = 48 B 에서 끝 → 둘째 헤더는 오프셋 32 에서 시작, 남은 16 B &lt; LEN 20</text>
  <text x="20" y="234" fill="currentColor" font-size="12">→ 커널: 첫 cmsg(creds)만 넣고 MSG_CTRUNC, fd 는 닫아 버린다 (VM 실측: controllen=32, fds 7→7)</text>
  <text x="20" y="262" fill="currentColor" font-size="12">Darwin 이라면 LEN 합 = SPACE 합 (4 B 정렬 + int 크기 데이터) → 같은 실수가 맥 테스트를 통과한다</text>
  <text x="20" y="284" fill="currentColor" font-size="12">교훈: 버퍼 = 각 cmsg 의 CMSG_SPACE 의 합. 단일 cmsg 라도 SPACE 로 잡아야 이식성이 생긴다</text>
</svg>
```

모델 함수는 세 줄이다:

```c
static size_t cmsg_len_abi(const cmsg_abi_t *a, size_t l)   { return align_up(a->hdr, a->align) + l; }
static size_t cmsg_space_abi(const cmsg_abi_t *a, size_t l) { return align_up(a->hdr, a->align) + align_up(l, a->align); }
/* fit: 남은 자리 ≥ LEN 이면 들어가고, 남은 자리에서 min(SPACE, 남은 자리)를 뺀다 (Linux put_cmsg 규칙) */
```

`demo_cmsg_bugs.c` 가 찍은 이 맥의 실제 값과 VM 의 `demo_linux.c` 값:

<!-- code: demo_cmsg_bugs.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_cmsg_bugs.c -o bugs && ./bugs     # 5번(정렬 위반)은 UB 를 일부러 실행한다
```

```text
$ ./bugs                                     # macOS 27.2, Apple clang 21.0.0
1) Darwin: sizeof(struct cmsghdr) = 12
   payload:      0   1   2   3   4   8  12  16
   CMSG_LEN:    12  13  14  15  16  20  24  28
   CMSG_SPACE:  12  16  16  16  16  20  24  28
$ ./demo_linux                               # Lima VM elinux: 7.0.0-38-generic, gcc 15.2.0, glibc 2.43
1) sizeof(struct cmsghdr)=16  CMSG_LEN(4)=20 SPACE(4)=24  LEN(8)=24 SPACE(8)=24  LEN(ucred)=28 SPACE(ucred)=32
3) two cmsgs (SO_PASSCRED on + 1 fd):
  sum of CMSG_LEN = 48 B             r=1 data="a" flags=0x8 controllen=32 fds 7->7 [cmsg len=28 CREDS]
  sum of CMSG_SPACE = 56 B           r=1 data="b" flags=0 controllen=56 fds 7->8 [cmsg len=28 CREDS] [cmsg len=20 RIGHTS]
```

Darwin 에서 LEN ≠ SPACE 인 것은 데이터가 4 의 배수가 아닐 때뿐이다(LEN(1)=13, SPACE(1)=16). fd 는 `int` 라 맥에서는 `CMSG_LEN` 으로 버퍼를 잡아도 절대 안 걸린다 → 리눅스 보드(aarch64 도 LP64)에서 처음 드러난다. 단일 cmsg 의 경우에는 리눅스도 `CMSG_LEN(4)=20` 버퍼에 fd 1개를 정상으로 넣었다(VM: `flags=0 controllen=20`) — 끝 패딩은 마지막 cmsg 에 필요 없기 때문이다. 문제는 **cmsg 가 둘 이상**(SO_PASSCRED, SO_TIMESTAMP 등을 켠 소켓)일 때다.

### Part B — 잘림과 누수: macOS 와 Linux 가 다르다

같은 실험(소켓쌍, pipe 끝을 보내고 여러 방식으로 받기)의 fd 표 변화. 맥은 `solution.c` B2 와 `demo_cmsg_bugs.c`, 리눅스는 `demo_linux.c` 실측:

| 받는 방식 | macOS 27.2 (XNU) | Linux 7.0 (VM) |
|---|---|---|
| fd 3개를 fd 1개 자리(SPACE(4))로 받기 | `MSG_CTRUNC`, `cmsg_len=24`(원래 길이), 보이는 fd 1개, **fd 표 +3** (안 보이는 2개 누수) | `MSG_CTRUNC`, 버퍼가 24 B 라 **fd 2개** 설치·보임, 셋째는 커널이 닫음 |
| control 버퍼를 `sizeof(int)`(4 B)로 | `MSG_CTRUNC`, cmsg 0개 보임, **fd 표 +1** | `MSG_CTRUNC`, fd 표 변화 0 |
| `recvmsg` control 없이 | `flags=0`(!), **fd 표 +1** | `MSG_CTRUNC`, 변화 0 |
| 그냥 `read()`/`recv()` | **fd 표 +1** | 변화 0 |
| 올바른 `recv_fds` + close | 변화 0 | — |

```text
$ ./bugs        (2) 부분, p=pipe s=socket)
  start                        0s 1f 2f 3s 4s 5p 6p
  recvmsg r=1 flags=0x20 (MSG_CTRUNC=0x20) controllen=16 cmsg_len=24 visible fd=7
  after truncated recvmsg      0s 1f 2f 3s 4s 5p 6p 7p 8p 9p
  read() got 1 byte 'R' and no cmsg
  after plain read()           0s 1f 2f 3s 4s 5p 6p 7p 8p 9p 10p
  after closing receiver sock  0s 1f 2f 5p 6p 7p 8p 9p 10p 11s 12s
```

즉 **XNU 는 fd 를 먼저 받는 프로세스 fd 표에 설치한 뒤에 control 버퍼로 복사하면서 자른다**(관찰에서 추론한 순서 — XNU 소스로 확인하지는 않았다). 그래서 맥에서는 받은 쪽이 아무리 조심해도 "상한보다 많이 보낸 fd" 는 보이지 않는 번호로 남는다. 악의적인 클라이언트가 메시지마다 fd 를 잔뜩 실어 보내면 서버의 `RLIMIT_NOFILE` 을 고갈시킬 수 있다는 뜻이다. 방어: (1) 프로토콜이 허용하는 최대 fd 수로 버퍼를 잡고, (2) `MSG_CTRUNC` 를 받으면 그 연결을 끊고(누수가 커지기 전에), (3) 신뢰할 수 없는 상대에게서 fd 를 받지 않는다(자격 확인 먼저 — Part C). 리눅스는 못 담은 fd 를 커널이 닫는다 — `unix(7)`(VM 의 man 페이지): "If the buffer used to receive the ancillary data containing file descriptors is too small (or is absent), then the ancillary data is truncated (or discarded) and the excess file descriptors are automatically closed in the receiving process." VM 실측도 그대로다. 같은 페이지에 따르면 한 메시지의 fd 상한 `SCM_MAX_FD` = 253 이고, 전송 중(in-flight) fd 가 `RLIMIT_NOFILE` 을 넘으면 `sendmsg` 가 `ETOOMANYREFS`(4.5 이후) — 맥의 상한은 확인하지 않았다.

`man 4 unix`(macOS)의 "Descriptors that are awaiting delivery, or that are purposely not received, are automatically closed by the system when the destination socket is closed" 는 **아직 큐에 있는** fd 이야기다. 이미 설치된 누수 fd 는 수신 소켓(3, 4번)을 닫아도 `7p 8p 9p 10p` 그대로 남았다(위 마지막 줄).

"cmsg_len 을 믿는 수신기" 는 더 나쁘다. 버퍼에 fd 1개만 들어왔는데 `cmsg_len=24` 를 믿고 3개를 읽으면 나머지 두 칸은 지난 내용(여기선 0) — `(fds 7,0,0)`. 이 코드가 나중에 그 "fd" 들을 `close` 하면 **stdin 을 닫는다.** 실제로 `recv_fds` 의 `avail` 클램프를 빼는 변형을 돌리면, 상한을 넘는 fd 로 보고 `close(0)` 을 해 버려서 마지막 전체 fd 개수 검사(`fd_count() == base`)에서만 걸렸다(55/56).

정렬 안 된 버퍼(`struct { char tag; char buf[CMSG_SPACE(4)]; }` 의 `buf`)는 커널이 그대로 받아 준다(demo 5번: `control buffer at 0x16fc02a21 (addr % 4 = 1), recvmsg r=1 flags=0`). 깨지는 것은 C 쪽이다 — `CMSG_FIRSTHDR` 가 어긋난 `struct cmsghdr *` 를 돌려주고, 그걸 읽는 것이 정의되지 않은 동작이다. arm64 하드웨어는 어긋난 로드를 허용해서 `-O2` 에서는 멀쩡해 보인다. UBSan 은 짚는다:

```text
$ cc -std=c11 -g -O1 -fsanitize=undefined demo_cmsg_bugs.c -o bugs_ub && ./bugs_ub
demo_cmsg_bugs.c:175:27: runtime error: member access within misaligned address 0x00016b052a21 for type 'struct cmsghdr', which requires 4 byte alignment
demo_cmsg_bugs.c:175:27: runtime error: load of misaligned address 0x00016b052a21 for type 'socklen_t' (aka 'unsigned int'), which requires 4 byte alignment
demo_cmsg_bugs.c:176:18: runtime error: load of misaligned address 0x00016b052a2d for type 'int', which requires 4 byte alignment
```

Cortex-M0/M0+ 같은 코어에서는 어긋난 워드 로드가 HardFault 다(ARMv6-M 은 비정렬 접근 미지원). RTOS 위 네트워크 스택에서 cmsg 비슷한 TLV 버퍼를 `char[]` 로 잡았다가 포팅 후 터지는 것과 같은 이야기 — C-B12 의 정렬 규칙.

### Part B — 메시지 경계와 fd 의 위치

| 소켓 | `send` 10, 20, 30 B 뒤 수신 | 작은 버퍼(5 B)로 20 B 받기 |
|---|---|---|
| `SOCK_STREAM` | `recv(128)` 한 번에 **60** | 5 B 받고 나머지 15 B 는 다음 `recv` 로 (안 버려짐) |
| `SOCK_DGRAM` | 10, 20, 30 (send 1번 = recv 1번) | 5 B, 나머지 버려짐, `MSG_TRUNC` |
| `SOCK_SEQPACKET` | macOS: `socketpair` → `EPROTONOSUPPORT` (`man 4 unix` 도 STREAM/DGRAM 만 언급) | Linux: 10, 그리고 5 B + `MSG_TRUNC` (VM) |

그래서 스트림 위 프로토콜은 프레이밍이 필요하다. `send_record` = `[u32 BE 길이][payload]`, `recv_record` 는 헤더 `read_all` → 0 바이트면 깨끗한 EOF(0), 1~3 바이트면 중간 EOF(`EPIPE`), 길이 > cap 이면 `EMSGSIZE`(읽지 않는다). 테스트: 10/20/30 레코드 3개가 그대로 + EOF, 60 B 레코드를 16 B 버퍼로 → `EMSGSIZE`, "길이 9 라더니 2 바이트 뒤 EOF" → `EPIPE`.

fd 가 실린 바이트는 스트림 어디에 붙나? `abc`, `def`, `G`+fd, `hij` 를 보내고 `recvmsg` 를 반복하면:

| | 첫 `recvmsg` | 둘째 `recvmsg` |
|---|---|---|
| macOS (B4 실측) | `abcdef` (6 B, fd 없음) | `Ghij` (4 B, **fd**) |
| Linux (VM) | `abcdefG` (7 B, **fd**) | `hij` (3 B) |

두 OS 모두 fd 는 `G` 를 담은 호출에 정확히 한 번 오지만, 경계를 어느 쪽에 긋는지가 다르다. 그래서 테스트는 이식 가능한 성질("fd 는 G 를 담은 recvmsg 에서 한 번")만 CHECK 한다. 실무 규칙: **fd 를 받을 수 있는 소켓에서는 모든 읽기를 `recvmsg`(control 버퍼 포함)로 한다** — 한 번이라도 `read` 로 읽으면 맥에서는 fd 가 새고 리눅스에서는 fd 가 사라진다.

### Part C — 피어 자격

```c
if (getpeereid(sock, &uid, &gid) < 0) return -1;                         /* STREAM 전용, DGRAM 은 EINVAL */
struct xucred xc; socklen_t l = sizeof xc;
if (getsockopt(sock, SOL_LOCAL, LOCAL_PEERCRED, &xc, &l) < 0) return -1;
if (l != sizeof xc || xc.cr_version != XUCRED_VERSION || xc.cr_uid != uid) { errno = EINVAL; return -1; }
getsockopt(sock, SOL_LOCAL, LOCAL_PEERPID, &pid, &l);                    /* Darwin 확장 */
```

- 이 맥의 실제 값: `uid=501 gid=20 ngroups=15`(C2), `sizeof(struct xucred)` = 76. `xucred` 는 `sys/ucred.h` 에 있고 `cr_groups[0]` 이 egid 다.
- 무엇이 언제 찍히나: `man getpeereid`/`man 4 unix`(macOS) — 서버가 받는 것은 클라이언트가 **connect 할 때** 의 유효 자격, 클라이언트가 받는 것은 서버가 **listen 할 때** 의 자격. listen 소켓 자체에는 `ENOTCONN`. socketpair 에서도 동작한다(자기 자신의 uid, LOCAL_PEERPID = 만든 프로세스).
- DGRAM socketpair: `getpeereid`, `LOCAL_PEERCRED` 모두 `EINVAL`, 그런데 `LOCAL_PEERPID` 는 성공한다. 그래서 `peer_identity` 는 셋 다 성공해야 0 을 돌려준다. `demo_cmsg_bugs.c` 4번 실측:

```text
4) peer credential facts (sizeof(struct xucred) = 76):
  getpeereid(listening socket) = -1 errno=57 (Socket is not connected)
  socketpair LOCAL_PEERPID = 1435 (me 1435)
  DGRAM socketpair: getpeereid=-1 errno=22, LOCAL_PEERCRED=-1 errno=22, LOCAL_PEERPID=0 pid=1435
  bind with 104 non-NUL path bytes = 0 errno=0, file created = yes
```

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">"상대 pid" 가 가리키는 것 — 같은 연결, 시간에 따라 (실측)</text>
  <line x1="40" y1="70" x2="720" y2="70" stroke="currentColor" stroke-width="2"/>
  <rect x="50" y="56" width="120" height="28" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="58" y="75" fill="currentColor" font-size="12">C: connect + write</text>
  <rect x="210" y="56" width="120" height="28" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="218" y="75" fill="currentColor" font-size="12">C: fork G, exit</text>
  <rect x="370" y="56" width="140" height="28" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="378" y="75" fill="currentColor" font-size="12">서버가 조회 ①</text>
  <rect x="540" y="56" width="170" height="28" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="548" y="75" fill="currentColor" font-size="12">G: write → 서버 조회 ②</text>
  <text x="20" y="120" fill="currentColor" font-weight="bold">macOS LOCAL_PEERPID</text>
  <rect x="370" y="104" width="140" height="26" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="380" y="122" fill="currentColor" font-family="monospace" font-size="12">C (90196)</text>
  <rect x="540" y="104" width="170" height="26" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="550" y="122" fill="currentColor" font-family="monospace" font-size="12">G (90197) ← 바뀜</text>
  <text x="20" y="166" fill="currentColor" font-weight="bold">Linux SO_PEERCRED</text>
  <rect x="370" y="150" width="140" height="26" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="380" y="168" fill="currentColor" font-family="monospace" font-size="12">C (292211)</text>
  <rect x="540" y="150" width="170" height="26" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="550" y="168" fill="currentColor" font-family="monospace" font-size="12">C (이미 죽음)</text>
  <text x="20" y="206" fill="currentColor" font-size="12">macOS: "마지막으로 그 소켓을 쓴 프로세스" — 계속 바뀐다.</text>
  <text x="20" y="226" fill="currentColor" font-size="12">Linux: connect 시점 스냅숏 — 죽은 pid 일 수 있고, 재사용될 수 있다.</text>
  <text x="20" y="252" fill="currentColor" font-size="12">어느 쪽이든 pid 는 신원이 아니다 → 인가는 uid 로, pid 는 로그용. 더 강하게: audit token / pidfd.</text>
</svg>
```

macOS 쪽 근거는 실측뿐이다(C4 테스트, 5회 모두 같은 결과). XNU 가 소켓마다 "마지막 사용자 pid"(`last_pid`)를 갱신하고 `LOCAL_PEERPID` 가 그것을 돌려준다고 알고 있지만 소스에서 확인하지는 않았다. 맥에서 강한 신원이 필요하면 `LOCAL_PEERTOKEN`(audit token — pid 와 pid 버전을 함께 담아 재사용을 구별, SDK 헤더에 상수 존재 확인)이나 XPC(OS-A14)를 쓴다. 리눅스는 `SO_PEERPIDFD`(헤더에 정의 존재 확인; 6.5 이후 커널로 알고 있음 — 미확인)로 pid 재사용을 피한다.

`allow_peer` 규칙과 이유:

| 입력 uid | 결과 | 이유 |
|---|---|---|
| 목록에 있음 | 허가 | 명시적 허용 목록(기본 거부) |
| 0 (root) | `allow_root` 일 때만 | root 를 목록에 "실수로" 넣는 것과 정책으로 허용하는 것을 구분 |
| `(uid_t)-1` | 항상 거부 | "모름" — 리눅스에서 연결 안 된 소켓의 `SO_PEERCRED` 는 오류 없이 `uid=-1, pid=0` 을 준다(VM 실측) |
| 그 밖 | 거부 | |

`serve_one` 은 이 레슨 전체를 합친 것이다: `accept` → `peer_identity` → `allow_peer` → 허가면 `send_fds(c, &dev_fd, 1, "OK", 2)`, 거부면 `send_fds(c, NULL, 0, "NO", 2)` → `close`. 거부를 "그냥 close" 로 하면 클라이언트는 서버가 죽은 것인지 거부한 것인지 모른다 — 변형 실험에서 C3 거부 검사가 잡았다. 실제 장비에서는 root 데몬이 `/dev/i2c-1`, `/dev/spidev0.0`, GPIO chardev 를 열어 두고 정해진 서비스 계정에게만 fd 를 준다. 앱은 root 권한도, 장치 파일 접근 권한(udev 규칙, ELX-M11)도 없이 그 장치를 쓴다. systemd-logind 가 세션에 DRM/input 장치 fd 를 D-Bus(SCM_RIGHTS)로 넘기는 것이 같은 패턴이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution    # 56/56 passed, 0.2 초 이내
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_asan && ./so_asan   # 56/56, 경고 0
```

스레드를 쓰지 않으므로 TSan 은 해당 없다. 리눅스 대조 데모(`/* ENV: linux */`, 맥 검증기는 건너뛴다 — 홈은 VM 에 읽기 전용으로 마운트되므로 복사해서 빌드):

<!-- code: demo_linux.c -->

```bash
limactl shell elinux -- bash -lc 'mkdir -p /tmp/OS-M16 && cp <lesson dir>/* /tmp/OS-M16/ && cd /tmp/OS-M16 \
  && gcc -std=c11 -Wall -Wextra -Werror -O2 demo_linux.c -o demo_linux && ./demo_linux'
```

```text
2) truncation / ignoring cmsg (MSG_CTRUNC=0x8 on Linux):
  3 fds into CMSG_SPACE(4)=24 B buf  r=1 data="T" flags=0x8 controllen=24 fds 7->9 [cmsg len=24 RIGHTS]
  1 fd into sizeof(int) buf          r=1 data="S" flags=0x8 controllen=0 fds 7->7
  1 fd, recvmsg without control      r=1 data="N" flags=0x8 controllen=0 fds 7->7
  1 fd, plain read()                 r=1 fds 7->7
  1 fd into CMSG_LEN(4)=20 B buf     r=1 data="L" flags=0 controllen=20 fds 7->8 [cmsg len=20 RIGHTS]
  stream sendmsg with 0 data bytes + fd: r=0 errno=0
5) SO_PEERCRED (me: pid=292210 uid=501):
  stream socketpair: pid=292210 uid=501 gid=1000
  dgram socketpair: r=0 pid=292210
  named, connector C=292211 exited: pid=292211
  after grandchild G=292212 wrote 'G':  pid=292211   (macOS LOCAL_PEERPID would say G)
  unconnected socket: r=0 pid=0 uid=-1  (no error!)
6) misc:
  SCM_CREDENTIALS without sender doing anything: pid=292210 uid=501 gid=1000
  MSG_CMSG_CLOEXEC: received fd 13 has FD_CLOEXEC=1
  SOCK_SEQPACKET socketpair r=0: recv -> 10, recv(5) of 20 B -> 5 flags=0x20 (MSG_TRUNC=0x20)
  abstract socket "@OS-M16.292210": bind=0 connect=0, file in /tmp? none — vanishes with the last fd
```

리눅스에서 눈여겨볼 것: `MSG_CTRUNC` 값이 0x8(맥 0x20) — 상수 값을 하드코딩하면 안 된다. DGRAM socketpair 에도 `SO_PEERCRED` 가 된다(맥 `getpeereid` 는 `EINVAL`). `SCM_CREDENTIALS` 는 받는 쪽이 `SO_PASSCRED` 만 켜면 보내는 쪽이 아무것도 안 해도 커널이 붙인다(보내는 쪽이 직접 `SCM_CREDENTIALS` 를 넣을 때는 자기 pid·real/effective/saved uid·gid 만 허용, 다른 값은 CAP_SYS_ADMIN/CAP_SETUID/CAP_SETGID 가 있어야 한다 — `unix(7)`, 여기선 미실험). 추상 이름공간(`sun_path[0] = '\0'`)은 파일을 안 만들어 `unlink` 가 필요 없지만, 파일 권한으로 접근을 못 막으므로 **자격 확인이 유일한 방어** 다. 스트림에 데이터 0 바이트 + fd 는 리눅스가 `r=0` 으로 받아 주었지만 그 fd 는 이후 어느 수신에서도 보이지 않았다(위 3·4번의 fd 개수) — 맥은 `EMSGSIZE`. 그래서 `send_fds` 는 1 바이트 이상을 요구한다.

## 7. 자주 하는 실수

정답 코드를 한 군데씩 바꿔 돌린 실제 결과:

| 실수 | 하네스 결과 (실측) | 왜 | 고친 코드 |
|---|---|---|---|
| `recv_fds` 에서 `MSG_CTRUNC` 검사 생략 | B2 잘림 검사 2개 FAIL — 54/56 | 잘린 메시지를 정상으로 넘겨 fd 1개를 돌려준다(나머지는 안 보이게 샌다) | `if ((msg.msg_flags & MSG_CTRUNC) \|\| bad)` → 전부 close, `EMSGSIZE` |
| `cmsg_len` 만 믿고 fd 개수 계산 | 마지막 `fd_count() == base` 만 FAIL — 55/56 | 버퍼 밖 0 을 fd 로 읽고 상한 초과분이라며 `close(0)` → **stdin 이 닫혔다** | `bytes = min(claimed, avail)` |
| 받은 fd 에 `FD_CLOEXEC` 를 안 켬 | A1 `cloexec == 1` FAIL — 55/56 | fd 플래그는 전달되지 않는다. exec 한 자식에게 장치 fd 가 샌다 | `fcntl(fd, F_SETFD, FD_CLOEXEC)` (리눅스는 `MSG_CMSG_CLOEXEC`) |
| `msg_controllen = sizeof 버퍼 전체` | `sendmsg` 가 `EINVAL` → 35/56 | 남는 0 바이트가 "길이 0 cmsg" 로 해석된다 (맥·리눅스 모두 실측) | `CMSG_SPACE(n * sizeof(int))` |
| `CMSG_SPACE` 를 `CMSG_LEN` 과 같게 계산 | B1 모델 검사 5개 FAIL — 51/56 (실제 송수신은 맥에서 전부 통과!) | Darwin 은 int 데이터면 둘이 같다 → 맥에서는 안 보이는 버그 | 데이터도 정렬: `align_up(hdr) + align_up(len)` |
| `allow_peer` 에서 root 를 따로 다루지 않음 | C1 FAIL — 55/56 | 목록에 0 이 없는데도 정책 실수가 그대로 반영되는 구조 | `if (uid == 0) return acl->allow_root;` |
| 거부할 때 "NO" 없이 그냥 close | C3 거부 검사 FAIL — 55/56 | 클라이언트가 거부와 서버 장애를 구분 못 한다 | 명시적 거부 응답 |
| `recv_record` 에서 0 바이트 헤더를 EOF 로 안 봄 | B3 프레이밍 FAIL — 55/56 | 정상 종료가 "레코드 중간 EOF" 오류로 보인다 | `if (r == 0) return 0;` |

그 밖에:

- **`char buf[CMSG_SPACE(sizeof(int))]` 를 지역 변수로** — 대부분 우연히 정렬되지만 보장은 없다. 구조체 안에 다른 필드 뒤에 두면 어긋난다(demo 5번). `union` 이나 `_Alignas(struct cmsghdr)`.
- **`*(int *)CMSG_DATA(c)`** — 정렬이 맞으면 동작하지만 strict aliasing·정렬 둘 다 걱정해야 한다. `memcpy` 가 공짜다(컴파일러가 로드 하나로 바꾼다).
- **받은 fd 를 검사 없이 사용** — 상대가 pipe 를 주면 `read` 가 영원히 막히고, 쓰기 가능한 fd 를 주면 데이터를 오염시킬 수 있다. `fstat` 종류 + `F_GETFL` 접근 모드 확인(연습 4).
- **소켓 파일을 안 지움** — 다음 실행의 `bind` 가 `EADDRINUSE`. 무조건 `unlink` 하면 살아 있는 서버의 파일을 지운다 → connect 시도로 생존 확인(연습 1).
- **pid 로 인가** — 위 그림. uid 로 하고 pid 는 로그로.
- **SIGPIPE** — 끊긴 클라이언트에게 `send` 하면 데몬이 죽는다. `signal(SIGPIPE, SIG_IGN)` 또는 `SO_NOSIGPIPE`(macOS)/`MSG_NOSIGNAL`(리눅스) — 하네스 `main` 첫 줄.

## 8. 면접 Q&A

<details>
<summary>Q1. fd 를 다른 프로세스로 "보낸다" 는 게 정확히 무슨 일인가요? 번호를 보내는 건가요?</summary>

번호가 아니라 그 번호가 가리키는 **열린 파일 설명(open file description)** 의 참조를 보낸다. `sendmsg` 때 커널이 보낸 쪽 fd 표에서 파일 객체를 찾아 참조 수를 올려 메시지에 붙이고, 받은 쪽 `recvmsg` 때 받은 프로세스 fd 표의 빈 칸에 설치한다(`dup` 과 같은 효과). 그래서 번호는 달라지고, 오프셋·상태 플래그(`O_APPEND` 등)는 공유되고(실측: 자식이 7 B 읽자 부모 오프셋 6 → 13), fd 플래그(`FD_CLOEXEC`)는 안 넘어간다. 꼬리: "보낸 쪽이 바로 close 하면?" → 큐가 참조를 쥐고 있어서 괜찮다(연습 3: 파일 unlink + fd close + 소켓 close 뒤에도 받은 쪽이 읽음). "fd 가 계속 큐에 쌓이고 순환 참조가 생기면?" → 리눅스는 unix 소켓 전용 GC(`unix_gc`)가 있다고 알려져 있다(미확인, 이름만).

</details>

<details>
<summary>Q2. CMSG_LEN 과 CMSG_SPACE 의 차이는? 버퍼를 CMSG_LEN 으로 잡으면 어떻게 되죠?</summary>

`CMSG_LEN(n)` = 정렬된 헤더 + n — `cmsg_len` 필드 값. `CMSG_SPACE(n)` = 정렬된 헤더 + 정렬된 n — 버퍼에서 차지하는 자리(다음 cmsg 가 정렬되도록 끝 패딩 포함). 리눅스 LP64 는 헤더 16 B·8 B 정렬이라 fd 1개면 LEN 20, SPACE 24. 버퍼를 LEN 합으로 잡으면 둘째 cmsg 부터 자리가 모자라 `MSG_CTRUNC` 가 나고 fd 는 커널이 닫는다(VM 실측: creds+fd 를 48 B 버퍼로 → creds 만 옴). 맥(Darwin)은 헤더 12 B·4 B 정렬이라 int 데이터면 LEN = SPACE 여서 같은 버그가 숨는다. 꼬리: "cmsg 가 하나면?" → 마지막 cmsg 는 LEN 이면 들어가서 리눅스에서도 동작했다(20 B 버퍼 OK). 하지만 소켓에 `SO_PASSCRED`·`SO_TIMESTAMP` 가 켜지는 순간 둘이 된다.

</details>

<details>
<summary>Q3. 받는 쪽 버퍼가 작아서 fd 가 다 안 들어가면 그 fd 들은 어떻게 되나요?</summary>

OS 마다 다르다. 리눅스(VM 실측): 들어가는 만큼 설치하고 나머지는 커널이 닫고 `MSG_CTRUNC`; control 없이 `read` 하면 fd 를 전부 버린다. macOS(실측): `MSG_CTRUNC` 는 켜지지만 **fd 는 전부 설치된다** — 보이지 않을 뿐. 3개를 1개 자리로 받으면 fd 표가 3 늘고 1개만 보인다. control 없이 `read` 해도 +1. 그래서 맥에서 fd 를 받는 서버는 (1) 프로토콜 최대치로 버퍼, (2) `MSG_CTRUNC` 면 연결 종료, (3) 인증 전에는 fd 를 받지 않는 프로토콜이어야 한다. 꼬리: "잘렸는데 cmsg_len 을 믿으면?" → 버퍼 밖 쓰레기를 fd 로 쓴다. 이 레슨에선 그게 0 이라 stdin 을 닫았다.

</details>

<details>
<summary>Q4. Unix 소켓 서버가 클라이언트를 어떻게 인증하나요? 클라이언트가 속일 수 있나요?</summary>

커널이 기록한 자격을 읽는다: macOS/BSD `getpeereid`, `LOCAL_PEERCRED`(xucred: euid, 그룹 목록), 리눅스 `SO_PEERCRED`(pid, uid, gid). 서버가 받는 값은 클라이언트가 connect 할 때의 유효 자격이고, 클라이언트는 그 시스템 콜을 다른 자격으로 부르는 것 말고는 바꿀 수 없다(`man getpeereid`). 그 위에 이름 있는 소켓이면 디렉터리·파일 권한(connect 에는 쓰기 권한 필요 — `man 4 unix`)이 1차 방어. 꼬리: "connect 한 뒤 setuid 로 권한을 내리면?" → 서버는 여전히 connect 시점 자격을 본다 — 그래서 "연결 후 권한 강하" 패턴에서는 자격이 실제 권한보다 높게 보일 수 있다. "연결된 fd 를 다른 프로세스에 넘기면?" → 자격은 원래 connect 한 쪽 것이 남는다. fd 는 '능력(capability)' 이라 가진 사람이 쓴다.

</details>

<details>
<summary>Q5. 그럼 상대 pid 로 "이 프로세스만 허용" 하면 안 되나요?</summary>

안 된다. 실측: macOS `LOCAL_PEERPID` 는 connect 한 C 가 아니라 **그 소켓을 마지막으로 쓴 손자 G** 로 바뀌었다. 리눅스 `SO_PEERCRED` 는 connect 시점 pid 를 계속 돌려줘서 C 가 이미 죽었는데도 C 를 가리켰다 — 그 pid 는 재사용될 수 있다. pid 를 받아 `/proc/<pid>/exe` 를 보고 판단하는 코드는 그 사이에 프로세스가 바뀌는 경쟁(TOCTOU)이 있다. 인가는 uid/gid 로, 더 강하게는 macOS audit token(`LOCAL_PEERTOKEN`, 코드 서명 확인과 결합), 리눅스 pidfd(`SO_PEERPIDFD`)나 SELinux 레이블(`SO_PEERSEC`)로. pid 는 로그·디버깅용.

</details>

<details>
<summary>Q6. SOCK_STREAM 과 SOCK_DGRAM, SOCK_SEQPACKET — fd 전달 프로토콜엔 뭘 쓰나요?</summary>

STREAM 은 경계가 없어서(10+20+30 → 60) 길이 프레이밍이 필요하고, fd 가 실린 바이트가 어느 `recvmsg` 에 올지 OS 마다 달랐다(맥: fd 앞에서 끊음, 리눅스: fd 뒤에서 끊음). DGRAM 은 메시지 하나 = 데이터 + fd 묶음이 정확히 함께 와서 fd 전달 프로토콜이 단순해지지만, 연결이 없어 맥에서는 `getpeereid` 를 못 쓴다(`EINVAL`). SEQPACKET 은 연결 + 경계 둘 다 — 리눅스에선 최선의 선택(VM: 지원, MSG_TRUNC 동작)이지만 **macOS 는 `EPROTONOSUPPORT`**. 이식성이 필요하면 STREAM + 프레이밍 + "모든 읽기를 recvmsg 로" 규칙, 리눅스 전용이면 SEQPACKET. 꼬리: "DGRAM 에서 잘리면?" → 나머지는 버려지고 `MSG_TRUNC`(맥 0x10, 리눅스 0x20).

</details>

<details>
<summary>Q7. 왜 이런 걸 실제로 쓰나요? 그냥 장치 파일 권한을 열어 주면 되지 않나요?</summary>

권한을 열면 그 uid 의 모든 프로세스가 장치를 영원히 쓸 수 있다. 브로커 패턴은 (1) 누구에게(자격 확인), (2) 언제(세션 중에만), (3) 어떤 모드로(O_RDONLY 로 열어서 넘기면 받은 쪽은 쓸 수 없다)를 데몬이 결정하게 한다. 예: systemd-logind 가 활성 세션에만 DRM·입력 장치 fd 를 주고 세션 전환 때 장치를 비활성화, Wayland 컴포지터, Android 의 권한 분리 서비스, 카메라/코덱 dma-buf fd 를 프로세스 사이에 복사 없이 넘기는 파이프라인(PipeWire, V4L2 → 추론 프로세스). 또 fd 전달은 무중단 재시작에 쓴다: 이전 프로세스가 listen 소켓을 새 프로세스에 넘기면 연결을 하나도 잃지 않는다(연습 2). 꼬리: "MMU 없는 RTOS 에선?" → 주소 공간이 하나라 포인터를 넘기면 되고, 같은 역할은 MPU 권한 분리 + SVC 게이트(SEC-M08)나 핸들 테이블이 한다.

</details>

<details>
<summary>Q8. macOS 와 Linux 차이를 한 표로 정리하면?</summary>

| 항목 | macOS (실측) | Linux (VM 실측) |
|---|---|---|
| `sizeof(cmsghdr)` / 정렬 | 12 / 4 | 16 / 8 |
| `MSG_CTRUNC` 때 못 담은 fd | 설치됨(누수) | 닫힘 |
| control 없이 read | fd 설치됨(누수) | 버림 + `MSG_CTRUNC` |
| 받은 fd 에 CLOEXEC | `fcntl` 로 따로 | `MSG_CMSG_CLOEXEC` |
| 피어 자격 | `getpeereid`, `LOCAL_PEERCRED`(xucred) | `SO_PEERCRED`(ucred: pid/uid/gid). `getpeereid` 는 glibc 헤더에 없다(VM `/usr/include` 검색: newlib 헤더에만) |
| 상대 pid | `LOCAL_PEERPID` = 마지막 사용자 | `SO_PEERCRED.pid` = connect 시점 |
| 메시지별 자격 | 없음(`SCM_CREDS` 상수는 있으나 미실험) | `SCM_CREDENTIALS` + `SO_PASSCRED` |
| SEQPACKET | 미지원 | 지원 |
| 추상 이름공간 | 없음 | `sun_path[0] = '\0'` |
| 데이터 0 B + fd (STREAM) | `EMSGSIZE` | `r=0`, 그 fd 는 이후 수신에 안 보임 |

</details>

## 9. 추가 연습

### 연습 1 — 남은 소켓 파일과 안전한 bind

서버가 `unlink` 없이 죽었다. 같은 경로로 `bind` 와 `connect` 를 해 보고, 죽은 파일만 치우고 다시 bind 하는 `bind_unix_fresh(path)` 를 만들어라. 살아 있는 서버의 파일은 지우면 안 된다. 경로가 `sun_path` 보다 길면?

<details>
<summary>답 보기</summary>

`bind` → `EADDRINUSE`, `connect` → `ECONNREFUSED`(파일은 있는데 듣는 소켓이 없다). `bind_unix_fresh`: bind 시도 → `EADDRINUSE` 면 connect 로 생존 확인 → 성공하면 누가 살아 있으니 `EADDRINUSE` 로 포기, `ECONNREFUSED` 면 `unlink` 후 재시도. (확인과 unlink 사이 경쟁은 남는다 — 실무는 lock 파일 `flock` 이나 systemd 소켓 활성화로 소유권을 정한다.) macOS `sun_path` 는 104 바이트(`sizeof sockaddr_un` = 106: `sun_len` + `sun_family` + 104). 실측(demo 4번 마지막 줄): 커널은 NUL 없이 꽉 찬 104 바이트 경로도 받아들여 파일을 만들었다 — 그래서 길이 검사는 우리 코드가 `strlen(path) >= sizeof sun_path` 이면 `ENAMETOOLONG` 으로 한다. 실측 출력: `ex1 stale path: bind -> EADDRINUSE, connect -> ECONNREFUSED; fresh bind ok; sizeof sun_path = 104`.

</details>

### 연습 2 — listen 소켓 넘겨주기 (무중단 교체)

부모가 **fork 뒤에** listen 소켓을 만들어 자식(새 워커)에게 SCM_RIGHTS 로 넘기고 자기 사본은 닫는다. 그 다음 부모가 클라이언트로 접속하면 누가 받나?

<details>
<summary>답 보기</summary>

자식이 받는다 — 듣는 소켓 객체는 하나이고 이제 자식 fd 표에만 있다. 자식이 `accept` 해서 `hi` 를 `HI` 로 돌려줬다(실측 `worker answered "HI"`). nginx/HAProxy 류의 바이너리 무중단 업그레이드, systemd 의 fd store(재시작해도 소켓 유지)가 이 원리다. 큐에 쌓여 있던 미수락 연결도 같이 넘어간다(소켓 객체가 같으므로).

</details>

### 연습 3 — 전송 중인 fd 와 FD_CLOEXEC

보낸 쪽이 파일에 `FD_CLOEXEC` 를 켜고, 보낸 직후 fd 를 닫고, 파일을 `unlink` 하고, 자기 소켓까지 닫는다. 받은 쪽은 읽을 수 있나? 받은 fd 의 `FD_CLOEXEC` 는?

<details>
<summary>답 보기</summary>

읽는다(`still here`). 큐에 있는 메시지가 파일 객체 참조를 쥐고 있고, unlink 된 파일은 마지막 참조가 사라질 때 지워진다(OSTEP 39장). `FD_CLOEXEC` 는 0 — fd 플래그는 프로세스별이라 전달되지 않는다(`man 4 unix` 문장 그대로, 실측 `FD_CLOEXEC on received fd = 0`). 본문 `recv_fds` 가 직접 켜는 이유다.

</details>

### 연습 4 — 받은 fd 를 믿기 전에

설정 파일을 받는 데몬이다. 상대가 읽기 전용 일반 파일 대신 pipe, 쓰기 가능한 파일, `/dev/null` 을 보낼 수도 있다. `accept_ro_regular(fd)` 를 만들어 걸러라.

<details>
<summary>답 보기</summary>

`fstat` 으로 `S_ISREG` 가 아니면 `EINVAL`(pipe/FIFO 는 `read` 가 영원히 막힐 수 있고, 장치는 읽기 자체가 부작용일 수 있다), `fcntl(F_GETFL) & O_ACCMODE` 가 `O_RDONLY` 가 아니면 `EACCES`(필요 이상의 권한은 받지 않는다 — 실수로 우리가 쓰는 것을 막는다). 실측 `4/4 classified`. 더 할 수 있는 것: 크기 상한(`st_size`), 소유자(`st_uid`) 확인, `mmap` 할 거라면 상대가 나중에 `ftruncate` 해서 `SIGBUS` 를 낼 수 있다는 점(리눅스는 memfd seal 로 막는다 — OS-B09 의 SIGBUS).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 11/11 passed, 0.1 초 이내
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_asan && ./pr_asan   # 11/11, 경고 0
```

## 10. 핵심 정리

- SCM_RIGHTS 는 fd 번호가 아니라 **열린 파일 설명** 의 참조를 보낸다 → 번호는 새로, 오프셋은 공유, `FD_CLOEXEC` 는 안 넘어옴(받고 나서 켠다; 리눅스는 `MSG_CMSG_CLOEXEC`).
- control 버퍼 = `union { struct cmsghdr; char buf[CMSG_SPACE(n)]; }`, `msg_controllen = CMSG_SPACE`, `cmsg_len = CMSG_LEN`, 데이터는 `CMSG_DATA` + `memcpy`. 남는 0 바이트를 controllen 에 포함하면 `EINVAL`.
- Darwin(12 B, 4 B 정렬)은 int 데이터에서 LEN = SPACE → `CMSG_LEN` 버퍼 버그가 **맥에서 숨고 리눅스(16 B, 8 B 정렬)에서 터진다**(creds+fd 48 B 버퍼 → `MSG_CTRUNC`).
- 받을 때: `MSG_CTRUNC` 검사, `cmsg_len` 대신 실제 들어온 길이, 상한 초과면 닫고 실패. **macOS 는 잘린·무시된 fd 도 설치한다(누수)**, 리눅스는 닫는다 — fd 를 받는 소켓의 모든 읽기는 `recvmsg`.
- STREAM 은 메시지를 합친다 → 길이 프레이밍. DGRAM 은 경계 보존 + `MSG_TRUNC`. SEQPACKET 은 리눅스만.
- 피어 자격: `getpeereid`/`LOCAL_PEERCRED`(맥), `SO_PEERCRED`(리눅스) = connect 시점 uid. **인가는 uid 로**, pid 는 로그용 — 맥 `LOCAL_PEERPID` 는 마지막 사용자, 리눅스 pid 는 죽은/재사용 pid 일 수 있다.
- 브로커 = 자격 확인 + 허용 목록(기본 거부, root 는 별도 플래그, uid −1 거부) + 필요한 모드로 연 fd 만 전달 + 명시적 거부 응답.

## 참고자료

- POSIX `sendmsg`, `recvmsg`, `<sys/socket.h>` (CMSG_DATA/NXTHDR/FIRSTHDR, MSG_CTRUNC, SCM_RIGHTS): <https://pubs.opengroup.org/onlinepubs/9699919799/functions/sendmsg.html>, <https://pubs.opengroup.org/onlinepubs/9699919799/functions/recvmsg.html>, <https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/sys_socket.h.html>
- POSIX `socketpair`, `bind`, `connect`(ECONNREFUSED), `fcntl`(FD_CLOEXEC 는 fd 플래그): <https://pubs.opengroup.org/onlinepubs/9699919799/functions/socketpair.html>, <https://pubs.opengroup.org/onlinepubs/9699919799/functions/fcntl.html>
- macOS man pages (이 맥에서 읽음): `unix(4)` (SCM_RIGHTS, 플래그 비전달, LOCAL_PEERCRED, 104 B 경로), `getpeereid(3)`, `recv(2)`, `sendmsg(2)`
- macOS SDK 헤더: `sys/socket.h` (cmsghdr 12 B, `__DARWIN_ALIGN32`, MSG_CTRUNC 0x20, SCM_CREDS 상수, MSG_CMSG_CLOEXEC 없음), `sys/un.h` (SOL_LOCAL, LOCAL_PEERCRED/PEERPID/PEEREPID/PEERUUID/PEERTOKEN), `sys/ucred.h` (struct xucred, XUCRED_VERSION)
- Linux man-pages `unix(7)` (SCM_RIGHTS, SCM_CREDENTIALS, SO_PEERCRED, SO_PASSCRED, abstract namespace, SEQPACKET), `cmsg(3)`, `recvmsg(2)` (MSG_CMSG_CLOEXEC): <https://man7.org/linux/man-pages/man7/unix.7.html>, <https://man7.org/linux/man-pages/man3/cmsg.3.html>, <https://man7.org/linux/man-pages/man2/recvmsg.2.html>
- W. R. Stevens, B. Fenner, A. Rudoff, *UNIX Network Programming Vol. 1* 3판 15장 (Unix Domain Protocols: descriptor passing, receiving sender credentials); Stevens & Rago, *APUE* 3판 17장 (Advanced IPC: passing file descriptors, `send_fd`/`recv_fd`)
- OSTEP 39장 *Files and Directories* (open file table, `dup`, unlink 과 참조 수) — 로컬: `operating-system-three-easy-pieces/book-md/C39_interlude_files_and_directories.md`
- 관련 레슨: OS-B02 (fd 상속·EOF 규칙), OS-B09 (mmap·SIGBUS), OS-B14 (IPC 비교 — Unix 소켓 기본), OS-M12 (I/O 모델 서버), OS-M15 (POSIX mq), OS-A11 (kqueue 데몬), OS-A13 (미니 RPC — 프레이밍 위의 요청/응답), OS-A14 (Mach 포트 권한·XPC), C-B12 (정렬·패딩), SEC-M08 (MPU 권한 분리), ELX-B13 (systemd 유닛), ELX-M11 (udev 장치 권한), BMC-M04 (D-Bus)
