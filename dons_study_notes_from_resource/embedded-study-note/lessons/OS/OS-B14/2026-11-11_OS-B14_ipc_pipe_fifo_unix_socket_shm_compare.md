---
id: OS-B14
time: 70분
updated: 2026-11-11
---
# OS-B14 IPC 비교: 파이프·FIFO·Unix 소켓·공유 메모리

## 학습 목표

- pipe, named FIFO, AF_UNIX `socketpair`, `shm_open`+`mmap` 링+이름 있는 세마포어(`sem_open`)로 **같은 채널 API**(create → fork → attach → send/recv → close)를 구현하고, 같은 테스트(핑퐁 에코 1 B..64 KB, 8 MB 대량 전송, 체크섬·순서 검사)를 네 수단 모두에 통과시킨다.
- 스트림 IPC 의 세 함정을 코드로 설명할 수 있다: **짧은 읽기**(`read_all`), **안 쓰는 fd 를 닫아야 EOF·EPIPE 가 온다**, **FIFO 를 여는 순서**.
- 이름 있는 IPC 객체(FIFO 경로, shm·sem 이름)는 프로세스보다 오래 산다는 것을 보이고, "만들고 바로 unlink" 패턴으로 누수 0 을 테스트한다.
- 왕복 지연(min/median/p99)과 MB/s 를 실측해 "작은 메시지는 커널 왕복 비용, 큰 데이터는 버퍼 크기·복사 횟수" 라는 결론을 숫자로 말한다.
- 상대가 죽었을 때 각 수단이 무엇을 보여 주는지(EOF / EPIPE / 아무것도 안 보임) 표로 정리하고 용도별 선택 기준을 세운다.

## 1. 문제

"프로세스 두 개가 데이터를 주고받아야 합니다. 무엇을 쓰고, 왜 그걸 고르죠?" — 임베디드 리눅스 면접에서 거의 반드시 나오는 질문이고, 답이 "공유 메모리가 제일 빠르니까요" 로 끝나면 꼬리질문에서 무너진다. 실제 장비에서는 센서 데몬 → 로거(파이프), 앱 → 워치독 데몬 하트비트(FIFO·소켓, ELX-B13), 설정 데몬 ↔ 여러 클라이언트(Unix 소켓, D-Bus 도 내부는 Unix 소켓 — BMC-M04), 카메라 프레임 → 추론 프로세스(공유 메모리, ROS-A05) 가 한 보드에 같이 산다. 멀티코어 MCU 의 코어 간 메일박스(PLAT-M05)도 결국 "공유 메모리 링 + 알림(doorbell)" 이고, 이 레슨의 shm 링과 같은 모양이다.

OS-B02 에서 파이프와 fd 상속을, OS-M02 에서 1 바이트 핑퐁으로 컨텍스트 스위치 비용을 봤다. 이번에는 **수단을 바꿔 가며 같은 일을 시키고** 차이를 잰다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">데이터가 지나가는 길 — 커널 버퍼 경유(복사 2번) vs 공유 페이지</text>
  <rect x="20" y="40" width="130" height="40" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="32" y="65" fill="currentColor">프로세스 A</text>
  <rect x="590" y="40" width="130" height="40" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="602" y="65" fill="currentColor">프로세스 B</text>
  <rect x="230" y="40" width="280" height="40" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="242" y="58" fill="currentColor" font-size="12">커널 버퍼 (이 맥 실측 용량)</text>
  <text x="242" y="74" fill="currentColor" font-size="12" font-family="monospace">pipe 64K · fifo 8K · sock 8K</text>
  <path d="M150 60 L228 60" stroke="currentColor" stroke-width="2"/>
  <path d="M220 54 L230 60 L220 66" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M510 60 L588 60" stroke="currentColor" stroke-width="2"/>
  <path d="M580 54 L590 60 L580 66" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="160" y="100" fill="currentColor" font-size="12">write: 복사 1</text>
  <text x="520" y="100" fill="currentColor" font-size="12">read: 복사 2</text>
  <text x="20" y="130" fill="currentColor">pipe / FIFO / Unix 소켓: 커널이 동기화(블록·깨움)와 경계·EOF 를 다 해 준다. 대신 매번 시스템 콜 + 복사.</text>
  <rect x="20" y="160" width="130" height="40" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="32" y="185" fill="currentColor">프로세스 A</text>
  <rect x="590" y="160" width="130" height="40" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="602" y="185" fill="currentColor">프로세스 B</text>
  <rect x="230" y="160" width="280" height="40" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="242" y="178" fill="currentColor" font-size="12">공유 물리 페이지 (양쪽 주소 공간에</text>
  <text x="242" y="194" fill="currentColor" font-size="12">mmap 된 같은 링, 방향당 8 × 64 KB)</text>
  <path d="M150 180 L228 180" stroke="currentColor" stroke-width="2"/>
  <path d="M510 180 L588 180" stroke="currentColor" stroke-width="2"/>
  <text x="160" y="220" fill="currentColor" font-size="12">memcpy (사용자 공간)</text>
  <text x="500" y="220" fill="currentColor" font-size="12">memcpy (생산자가 직접 쓰면 0)</text>
  <rect x="230" y="240" width="280" height="30" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="242" y="260" fill="currentColor" font-size="12">sem_post / sem_wait (여전히 커널 호출)</text>
  <text x="20" y="300" fill="currentColor">shm: 데이터는 커널을 안 거치지만 "언제 읽나" 알림·EOF·상대 사망 감지는 내 몫.</text>
  <text x="20" y="322" fill="currentColor">→ 작은 메시지는 알림 비용이 전부라 shm 이 이득이 없고, 큰 데이터에서만 복사·버퍼 크기 차이가 드러난다.</text>
</svg>
```

모든 테스트는 같은 채널 API 를 쓴다:

```c
int     chan_create(chan_t *c);            /* fork 전: pipe 2개 / mkfifo 2개 / socketpair / shm+sem */
int     chan_attach(chan_t *c, int side);  /* fork 뒤: 0 = 부모(클라이언트), 1 = 자식(서버) — 내 몫 고르기 */
ssize_t chan_send(chan_t *c, const void *b, size_t n);   /* 하네스: 스트림이면 write_all, shm 이면 shm_send */
ssize_t chan_recv(chan_t *c, void *b, size_t n);         /* 정확히 n 바이트, EOF 면 그보다 적게 */
void    chan_shutdown_write(chan_t *c), chan_close(chan_t *c);   /* 하네스 제공 */
```

### Part A — 바이트 스트림 3종: pipe · FIFO · socketpair

- `write_all` / `read_all`: n 바이트를 다 쓰고/읽을 때까지 반복, `EINTR` 재시도, `read` 가 0 이면 EOF 로 멈추고 읽은 만큼 반환.
- `chan_create`: pipe 는 **2개**(파이프는 단방향), FIFO 는 `/tmp/OS-B14_work/run.<pid>/` 에 `mkfifo` 2개, 소켓은 `socketpair(AF_UNIX, SOCK_STREAM)` 하나(양방향).
- `chan_attach`: 내 몫의 fd 를 고르고 **나머지를 닫는다**. FIFO 는 여기서 `open` 한다 — 양쪽이 같은 순서로 열어야 하고, 둘 다 열린 뒤에는 경로를 `unlink`.
- 테스트: 단위(짧은 읽기 200 KB, EOF 중간/처음, 닫힌 파이프에 쓰기 = `EPIPE`) + 수단별 핑퐁(9가지 크기 × 20 왕복, 헤더 `{magic, seq, len, FNV-1a}`) + 8 MB 대량(12345 바이트씩 쓰고 4 KB 씩 읽기, 총 바이트·FNV 비교) + **fd 누수 0** + **이름 남김 0**.

### Part B — 공유 메모리 링 + 이름 있는 세마포어

- `shm_create`: `shm_open(O_CREAT|O_EXCL)` → `ftruncate` → `mmap(MAP_SHARED)` → `close(fd)`, `sem_open` 4개(방향마다 items=0, space=8). fork 로 물려주므로 **만든 즉시** `shm_unlink`/`sem_unlink`.
- `shm_send`: 64 KB 씩 잘라 `sem_wait(space)` → 슬롯에 복사·`len` 기록 → `sem_post(items)`.
- `shm_recv`: 바이트 스트림 의미 — 슬롯 하나를 여러 `recv` 에 나눠 읽을 수 있어야 한다. `len == 0` 슬롯은 EOF 표시(하네스의 `chan_shutdown_write` 가 넣는다).
- 같은 핑퐁·대량·fd·이름 테스트를 통과해야 한다. macOS 에는 `sem_init` 이 없다(아래 실측) — 그래서 이름 있는 세마포어.

### Part C — 상대가 죽으면, 그리고 무엇을 고를까

- 서버가 `"hello"` 를 보내고 `SIGKILL` 로 즉사한다. 클라이언트가 그 뒤 쓰기·읽기를 하면 각 수단에서 무엇이 보이는지 체크한다: 스트림은 `EPIPE` + EOF, shm 은 쓰기는 성공하고 읽기는 **영원히** 기다린다(워치독 `SIGALRM` 이 끊는 것을 체크).
- `pick_ipc(need)` — 관계(부모-자식?), 방향, 클라이언트 수, fd 전달, 메시지 크기·처리량으로 수단을 고르는 규칙 함수. 7가지 시나리오 체크.
- 시간은 체크하지 않는다. 측정은 `demo_bench.c`.

## 2. Problem (English)

> **Same protocol, four IPC mechanisms.**
>
> Implement a bidirectional byte-stream channel between a parent and a forked child over (1) two pipes, (2) two named FIFOs, (3) an `AF_UNIX`/`SOCK_STREAM` socketpair, and (4) a POSIX shared-memory ring (`shm_open` + `mmap`, 8 slots × 64 KB per direction) synchronized with named semaphores (`sem_open`; `sem_init` is not available on macOS).
>
> ```c
> ssize_t write_all(int fd, const void *buf, size_t n);   // n or -1; retries EINTR
> ssize_t read_all(int fd, void *buf, size_t n);          // n, or fewer at EOF (0 = EOF at start), -1 on error
> int     chan_create(chan_t *c);                         // before fork
> int     chan_attach(chan_t *c, int side);               // after fork; close what this side does not use
> ssize_t shm_send(chan_t *c, const void *buf, size_t n); // stream semantics over fixed slots
> ssize_t shm_recv(chan_t *c, void *buf, size_t n);       // may consume part of a slot; len==0 slot = EOF
> ```
>
> Requirements: an echo test (1 B to 64 KB, 180 frames with sequence numbers and FNV-1a checksums) and an 8 MB one-way transfer must arrive intact and in order on every mechanism; no file descriptor may leak; no FIFO path, shm name or semaphore name may survive the test. Then measure round-trip latency (min/median/p99) and throughput (MB/s) and write a selection guide.
>
> Follow-ups: Why does a pipe reader hang when the writer has exited? Why does a FIFO `open` block? How does each mechanism report a dead peer? Is shared memory always faster? What does `PIPE_BUF` guarantee? Why `shutdown(SHUT_WR)` instead of `close` on a socket?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력, 0.15 초 — 멈추지 않는다):

```text
FAIL starter.c:481  !(uc & U_BASIC)
...
transport  pingpong       bulk 8MB       peer killed: send / recv
pipe       FAIL (create)  FAIL (create)  ? /
FAIL starter.c:501  !(a & (R_CREATE | R_ATTACH | R_DATA | R_SERVER))
...
pick: sensor proc | filter (fork, 1-way, 64 B)     -> ?
FAIL starter.c:526  p == cases[i].want
...
1/33 passed
```

통과하는 1개는 하네스 자체 점검(FNV-1a 표준 벡터 `"a"` → `0xe40c292c`)이다. 하네스 구조:

| 층 | 하는 일 | 왜 |
|---|---|---|
| 하네스(부모) | `run_isolated()`: 러너를 fork, 새 프로세스 그룹, `waitpid(WNOHANG)` 1 ms 폴링, 시간 초과면 그룹 전체 `SIGKILL`, 끝나면 이름 강제 삭제 | 하네스는 IPC 로 절대 블록하지 않는다 |
| 러너(자식) | `SIGPIPE` 무시, `setitimer` 워치독, 채널 생성 → 다시 fork → 클라이언트 역할, 끝나면 fd 개수·이름 검사 | 결과는 종료 코드 비트 + 진행 보고 파이프 |
| 서버(손자) | 자기 워치독(2 s), 에코 또는 받기만, EOF 에서 종료 | 타이머는 fork 로 상속되지 않으므로 따로 건다 |

- 결과 코드: `R_CREATE/ATTACH/DATA/SERVER/FDLEAK/NAMES` 비트. 러너가 죽으면(워치독) 모든 비트 실패로 친다.
- fd 누수 검사는 러너 안에서 `fcntl(fd, F_GETFD)` 로 0..255 를 세서 채널 전/후를 비교한다. macOS 에서는 `sem_open` 도 fd 를 하나 쓴다(`sem_open` 이 돌려준 포인터 값이 `0x3` 이었다) — `sem_close` 를 빼먹으면 이 검사에 걸린다.
- 이름 검사: `stat(FIFO 경로)`, `shm_open(이름, O_RDONLY)`, `sem_open(이름, 0)` 이 전부 실패해야 한다.

## 4. 힌트

<details>
<summary>힌트 1 — read 는 "요청한 만큼" 이 아니라 "지금 있는 만큼" 준다</summary>

`read(fd, buf, 65536)` 은 1 바이트만 줄 수도 있다. 소켓 기본 버퍼(이 맥 8 KB)로 64 KB 를 보내면 `read` 한 번이 8 KB 이하를 준다(`./bench reads` 실측: 64 KB × 256 번 썼는데 `read` 2048 번, 전부 4~8 KB). 그래서 프레임(헤더 + 페이로드)을 읽는 곳은 항상 루프다. `read` 가 0 이면 EOF 이므로 반드시 멈춘다 — `continue` 하면 무한 루프.

</details>

<details>
<summary>힌트 2 — EOF 는 "쓰기 끝이 전부 닫혔을 때" 온다</summary>

fork 하면 fd 가 전부 복제된다. 부모가 자기 쓰기 끝을 닫아도 **자식이 물려받은 쓰기 끝 사본** 이 열려 있으면 읽는 쪽은 EOF 를 못 받는다(OS-B02 의 그 버그). 그래서 attach 에서 내 몫(읽기 1개, 쓰기 1개) 말고는 전부 닫는다. 같은 이유로 읽는 끝이 남아 있으면 죽은 상대에게 써도 `EPIPE` 가 안 나온다.

</details>

<details>
<summary>힌트 3 — FIFO open 은 상대가 열 때까지 막힌다</summary>

`open(fifo, O_RDONLY)` 는 쓰는 쪽이, `O_WRONLY` 는 읽는 쪽이 열 때까지 기다린다(POSIX `open`, O_NONBLOCK 이 없을 때). 부모가 A 를 쓰기로 열며 기다리는데 자식이 B 를 먼저 열며 기다리면 둘 다 영원히 잔다. **두 쪽이 같은 순서**(p2c 먼저)로 열면 된다.

</details>

<details>
<summary>힌트 4 — shm 링은 OSTEP 31장의 empty/full 세마포어 버퍼 그대로다</summary>

`space`(빈 슬롯 수, 처음 8)와 `items`(찬 슬롯 수, 처음 0). 보내기 = `wait(space)` → 쓰기 → `post(items)`, 받기 = `wait(items)` → 읽기 → `post(space)`. 생산자·소비자가 각각 하나(SPSC)라서 `tail`/`head` 는 공유할 필요가 없다 — 각 프로세스의 지역 변수. 받기는 슬롯 하나를 여러 번에 나눠 읽을 수 있도록 "읽던 슬롯 + 오프셋" 을 기억한다.

</details>

<details>
<summary>힌트 5 — 이름은 언제 지우나</summary>

fork 로 물려줄 거라면 이름은 만든 순간 이후 필요 없다 → 바로 `unlink`. 매핑·세마포어·열린 FIFO 는 이름이 없어도 계속 동작한다(파일의 unlink 와 같다, OSTEP 39.9·39.13). 남남 프로세스라면 상대가 열었다는 확인(핸드셰이크) 뒤에 지운다.

</details>

## 5. 풀이

### Part A — write_all / read_all

```c
static ssize_t read_all(int fd, void *buf, size_t n) {
    char *p = buf; size_t done = 0;
    while (done < n) {
        ssize_t r = read(fd, p + done, n - done);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (r == 0) break;                  /* EOF: 거기까지 읽은 양을 돌려준다 */
        done += (size_t)r;
    }
    return (ssize_t)done;
}
```

`write_all` 도 같은 모양(0 반환 처리 없음). 블로킹 파이프·소켓에서 `write` 는 보통 전부 쓰고 돌아오지만(`./bench reads`: 짧은 쓰기 0회), 시그널로 끊기거나(SA_RESTART 없음, OS-B07) non-blocking fd 이면 일부만 쓰고 돌아올 수 있으므로 루프가 계약이다. 반대로 **짧은 읽기는 일상** 이다:

```text
$ ./bench reads     (64 KB 씩 256 번 write, 64 KB 버퍼로 read)
pipe      read() calls    256 (ideal 256), short writes 0 | =64K:256
sock      read() calls   2048 (ideal 256), short writes 0 | 4K-8K:2048
sock256k  read() calls    256 (ideal 256), short writes 0 | =64K:256
```

파이프는 버퍼가 64 KB 라 우연히 딱 맞았을 뿐이다. 하네스의 핑퐁은 헤더와 페이로드를 따로 쓰므로 파이프에서도 짧은 읽기가 나온다 — `read_all` 을 "read 한 번" 으로 바꾼 변형은 pipe/fifo/sock 모두 `data` 로 실패했다(7. 실수 표).

### Part A — create / attach: 누가 어떤 끝을 갖나

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">pipe 2개를 fork 뒤 나눠 갖기 — 회색 = attach 에서 닫는 끝</text>
  <rect x="20" y="40" width="200" height="180" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="32" y="62" fill="currentColor" font-weight="bold">부모 (side 0)</text>
  <rect x="520" y="40" width="200" height="180" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="532" y="62" fill="currentColor" font-weight="bold">자식 (side 1)</text>
  <rect x="40" y="80" width="160" height="26" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="50" y="98" fill="currentColor" font-family="monospace">fd[1] p2c 쓰기 ✓</text>
  <rect x="40" y="114" width="160" height="26" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="50" y="132" fill="currentColor" font-family="monospace">fd[0] p2c 읽기 ✗</text>
  <rect x="40" y="148" width="160" height="26" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="50" y="166" fill="currentColor" font-family="monospace">fd[2] c2p 읽기 ✓</text>
  <rect x="40" y="182" width="160" height="26" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="50" y="200" fill="currentColor" font-family="monospace">fd[3] c2p 쓰기 ✗</text>
  <rect x="540" y="80" width="160" height="26" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="550" y="98" fill="currentColor" font-family="monospace">fd[0] p2c 읽기 ✓</text>
  <rect x="540" y="114" width="160" height="26" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="550" y="132" fill="currentColor" font-family="monospace">fd[1] p2c 쓰기 ✗</text>
  <rect x="540" y="148" width="160" height="26" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="550" y="166" fill="currentColor" font-family="monospace">fd[3] c2p 쓰기 ✓</text>
  <rect x="540" y="182" width="160" height="26" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="550" y="200" fill="currentColor" font-family="monospace">fd[2] c2p 읽기 ✗</text>
  <rect x="290" y="80" width="160" height="40" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="300" y="104" fill="currentColor">p2c 파이프 (커널)</text>
  <rect x="290" y="150" width="160" height="40" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="300" y="174" fill="currentColor">c2p 파이프 (커널)</text>
  <path d="M200 93 L288 100" stroke="currentColor" stroke-width="2"/>
  <path d="M450 100 L538 93" stroke="currentColor" stroke-width="2"/>
  <path d="M538 161 L452 170" stroke="currentColor" stroke-width="2"/>
  <path d="M288 170 L200 161" stroke="currentColor" stroke-width="2"/>
  <text x="10" y="248" fill="currentColor">p2c 의 쓰기 끝이 부모 것 하나뿐이어야 부모가 close 할 때 자식이 EOF 를 본다.</text>
  <text x="10" y="270" fill="currentColor">p2c 의 읽기 끝이 자식 것 하나뿐이어야 자식이 죽었을 때 부모의 write 가 EPIPE 가 된다.</text>
  <text x="10" y="292" fill="currentColor">socketpair 는 fd 하나가 양방향: 부모 fd[0], 자식 fd[1] — 남의 것 하나만 닫으면 된다.</text>
</svg>
```

| 수단 | create (fork 전) | attach 부모 | attach 자식 | 이름 정리 |
|---|---|---|---|---|
| pipe | `pipe(fd+0)`, `pipe(fd+2)` | r=fd[2], w=fd[1], 나머지 close | r=fd[0], w=fd[3], 나머지 close | 이름 없음 |
| FIFO | `mkfifo` ×2 | `open(p2c, W)` → `open(c2p, R)` → `unlink` ×2 | `open(p2c, R)` → `open(c2p, W)` | 부모가 둘 다 연 뒤 |
| sock | `socketpair(AF_UNIX, SOCK_STREAM)` | r=w=fd[0], close fd[1] | r=w=fd[1], close fd[0] | 이름 없음 |
| shm | `shm_create` (아래) | tx=dir[0], rx=dir[1] | tx=dir[1], rx=dir[0] | 만든 즉시 |

FIFO 를 부모가 `unlink` 해도 되는 시점: 부모의 두 번째 `open(c2p, O_RDONLY)` 는 자식이 `open(c2p, O_WRONLY)` 를 해야 돌아온다 = 자식의 마지막 open. 그러니 그 뒤엔 아무도 경로로 찾을 일이 없다.

**FIFO 는 macOS 에서 사실상 소켓이다.** XNU 소스 `bsd/miscfs/fifofs/fifo_vnops.c` 의 `fifo_open` 은 `socreate(AF_LOCAL, ..., SOCK_STREAM)` 두 개를 만들고 `soconnect2` 로 잇는다. 실측도 맞아떨어진다: 용량이 pipe 는 65536, FIFO 는 8192 = socketpair 기본값과 같다(연습 3). `fstat` 은 그래도 `S_ISFIFO` 를 돌려주고 `getsockopt` 는 `ENOTSOCK` 이라 버퍼를 키울 방법이 없다. (리눅스의 FIFO 는 pipe 와 같은 pipefs 구현이라 이 차이가 없다 — 리눅스는 이 레슨에서 재지 않았다.)

### Part B — shm 링

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">shm_area_t: 방향마다 슬롯 8개 링 + 세마포어 2개 (SPSC)</text>
  <text x="10" y="56" fill="currentColor" font-family="monospace">dir[0]</text>
  <g stroke="currentColor">
    <rect x="70" y="40" width="70" height="28" class="f-acc"/><rect x="140" y="40" width="70" height="28" class="f-acc"/>
    <rect x="210" y="40" width="70" height="28" class="f-acc"/><rect x="280" y="40" width="70" height="28" class="f-mut"/>
    <rect x="350" y="40" width="70" height="28" class="f-mut"/><rect x="420" y="40" width="70" height="28" class="f-mut"/>
    <rect x="490" y="40" width="70" height="28" class="f-mut"/><rect x="560" y="40" width="70" height="28" class="f-mut"/>
  </g>
  <text x="78" y="59" fill="currentColor" font-size="12">len|data</text>
  <text x="640" y="59" fill="currentColor" font-size="12">부모 → 자식</text>
  <text x="70" y="88" fill="currentColor" font-size="12">↑ rx_head (자식, 지역 변수) + rx_off</text>
  <text x="280" y="106" fill="currentColor" font-size="12">↑ tx_tail (부모, 지역 변수)</text>
  <rect x="70" y="120" width="250" height="44" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="80" y="138" fill="currentColor" font-family="monospace">items: 찬 슬롯 수 (처음 0)</text>
  <text x="80" y="156" fill="currentColor" font-size="12">받는 쪽이 wait, 보내는 쪽이 post</text>
  <rect x="380" y="120" width="250" height="44" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="390" y="138" fill="currentColor" font-family="monospace">space: 빈 슬롯 수 (처음 8)</text>
  <text x="390" y="156" fill="currentColor" font-size="12">보내는 쪽이 wait, 받는 쪽이 post</text>
  <text x="10" y="196" fill="currentColor">send: wait(space) → memcpy, len=k → tail++ → post(items)   (64 KB 넘으면 조각마다 반복)</text>
  <text x="10" y="218" fill="currentColor">recv: (읽던 슬롯 없으면 wait(items)) → 일부/전부 복사 → 다 읽으면 head++ → post(space)</text>
  <text x="10" y="240" fill="currentColor">len == 0 슬롯 = EOF. 받은 쪽은 그 슬롯을 놓지 않아서 다음 recv 도 EOF.</text>
  <text x="10" y="262" fill="currentColor">dir[1] 은 반대 방향 (자식 → 부모), 세마포어 sem[2], sem[3]. 링 전체 2 × 8 × 64 KB ≈ 1 MB.</text>
</svg>
```

- **메모리 순서는 누가 보장하나?** POSIX XBD 4.12 *Memory Synchronization* 은 `sem_post`, `sem_wait` 를 "메모리를 동기화하는 함수" 목록에 넣는다. 보내는 쪽의 `memcpy` + `len` 쓰기 → `sem_post(items)` → 받는 쪽 `sem_wait(items)` → 읽기 순서가 보장되므로 atomics 가 필요 없다. 세마포어 없이 플래그로 돌리려면 C11 acquire/release 가 필요하다(DS-A01, OS-M08).
- **링 커서는 왜 공유 안 하나?** 생산자만 `tail` 을, 소비자만 `head` 를 바꾼다. 상대의 진행 상황은 세마포어 값이 알려 준다(`space` = 8 − 미처리 슬롯). 그래서 커서를 지역 변수로 두면 거짓 공유(false sharing)도 없다.
- **macOS 사실(실측):** `sem_init` → `-1 / ENOSYS` (헤더에 `__deprecated`). `shm_open` 이름은 `/` 포함 31 바이트까지(32 바이트면 `ENAMETOOLONG`, `PSHMNAMLEN 31`). shm 객체에 두 번째 `ftruncate` 는 `EINVAL`, `write()` 는 `ENXIO` — 크기는 한 번에 정하고 `mmap` 으로만 쓴다. `sem_unlink` 뒤에도 fork 로 물려받은 세마포어는 동작하고, 같은 이름으로 다시 `sem_open` 하면 `ENOENT`.

### 정답 실행

```text
$ ./solution      (0.7 초, 6회 연속 + 다른 cwd 에서 1회 모두 33/33)
transport  pingpong       bulk 8MB       peer killed: send / recv
pipe       OK             OK             EPIPE / EOF
fifo       OK             OK             EPIPE / EOF
unix-sock  OK             OK             EPIPE / EOF
shm+sem    OK             OK             ok (buffered) / HANG (watchdog SIGALRM)
pick: sensor proc | filter (fork, 1-way, 64 B)     -> pipe
pick: any app -> watchdog daemon heartbeat         -> fifo
pick: parent <-> worker request/response           -> socketpair
pick: config daemon, many clients                  -> unix-listen
pick: parent hands opened fds to a child           -> socketpair
pick: camera 1080p frames 6 MB @ 30 fps            -> shm
pick: radar samples 4 KB, 3 GB/s                   -> shm
33/33 passed
```

### 측정 — `demo_bench.c`

<!-- code: demo_bench.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o bench   # solution.c 를 #include 한다 (같은 폴더)
./bench rtt 5 3000 ; ./bench bulk 5 256 64 ; ./bench bulk 5 256 4 ; ./bench reads ; ./bench pair 5
```

환경: Apple M6 (`sysctl -n machdep.cpu.brand_string`), macOS 27.2, `Apple clang version 21.0.0`. **측정 중 load average 13~42** (다른 작업 10여 개가 병렬로 돌고 있었다) — 중앙값·p99 는 부풀었고 run 간 흔들림이 크다. 최솟값과 **수단 사이의 비율** 이 믿을 만한 정보다. macOS 는 코어 고정이 안 된다(OS-M02).

**왕복 지연 (ns) — 클라이언트가 n 바이트 보내고 같은 n 바이트를 돌려받을 때까지.** 5 run × 3000 왕복(처음 300 버림), 각 통계의 run 간 중앙값 (min 은 최솟값):

```text
size      | pipe                     | fifo                     | sock                     | sock256k                 | shm+sem
          |     min     med      p99 |     min     med      p99 |     min     med      p99 |     min     med      p99 |     min     med      p99
1         |    2375    6125    93375 |    5583   10875    50792 |    2500    5292    27583 |    2667    6167    27625 |    3167    6458    26125
64        |    2375    6250    94208 |    5750   10792    34667 |    2625    5583    20625 |    2583    4875    25167 |    3208    6458    28250
1024      |    2417    6667    53583 |    5667   10750    36958 |    2667    5500    18875 |    2583    6375    28708 |    3292    6125    24000
4096      |    2625    6458   104417 |    6042   11917    33667 |    2708    8292    23958 |    2708    7458    39167 |    3458    6750    31667
16384     |    3083    8125   152333 |   12708   34792    68042 |    8250   22292    47042 |    3583    9542    37083 |    5125    8792    30583
65536     |    7250   15917   121292 |   39125   95750   252750 |   43125  107125   240583 |    8417   13917    70042 |    7583   11833    51042
```

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">왕복 지연 최솟값 (ns) — 5 run × 3000 왕복, 실측 (Apple M6, 부하 있음)</text>
  <text x="10" y="67" fill="currentColor">pipe</text>
  <rect x="110" y="40" width="28.5" height="14" class="f-ok" stroke="currentColor"/>
  <text x="144.5" y="52" fill="currentColor" font-size="12">2375</text>
  <rect x="110" y="57" width="31.5" height="14" class="f-acc" stroke="currentColor"/>
  <text x="147.5" y="69" fill="currentColor" font-size="12">2625</text>
  <rect x="110" y="74" width="87.0" height="14" class="f-warn" stroke="currentColor"/>
  <text x="203.0" y="86" fill="currentColor" font-size="12">7250</text>
  <text x="10" y="129" fill="currentColor">fifo</text>
  <rect x="110" y="102" width="67.0" height="14" class="f-ok" stroke="currentColor"/>
  <text x="183.0" y="114" fill="currentColor" font-size="12">5583</text>
  <rect x="110" y="119" width="72.5" height="14" class="f-acc" stroke="currentColor"/>
  <text x="188.5" y="131" fill="currentColor" font-size="12">6042</text>
  <rect x="110" y="136" width="469.5" height="14" class="f-warn" stroke="currentColor"/>
  <text x="585.5" y="148" fill="currentColor" font-size="12">39125</text>
  <text x="10" y="191" fill="currentColor">sock</text>
  <rect x="110" y="164" width="30.0" height="14" class="f-ok" stroke="currentColor"/>
  <text x="146.0" y="176" fill="currentColor" font-size="12">2500</text>
  <rect x="110" y="181" width="32.5" height="14" class="f-acc" stroke="currentColor"/>
  <text x="148.5" y="193" fill="currentColor" font-size="12">2708</text>
  <rect x="110" y="198" width="517.5" height="14" class="f-warn" stroke="currentColor"/>
  <text x="633.5" y="210" fill="currentColor" font-size="12">43125</text>
  <text x="10" y="253" fill="currentColor">sock256k</text>
  <rect x="110" y="226" width="32.0" height="14" class="f-ok" stroke="currentColor"/>
  <text x="148.0" y="238" fill="currentColor" font-size="12">2667</text>
  <rect x="110" y="243" width="32.5" height="14" class="f-acc" stroke="currentColor"/>
  <text x="148.5" y="255" fill="currentColor" font-size="12">2708</text>
  <rect x="110" y="260" width="101.0" height="14" class="f-warn" stroke="currentColor"/>
  <text x="217.0" y="272" fill="currentColor" font-size="12">8417</text>
  <text x="10" y="315" fill="currentColor">shm+sem</text>
  <rect x="110" y="288" width="38.0" height="14" class="f-ok" stroke="currentColor"/>
  <text x="154.0" y="300" fill="currentColor" font-size="12">3167</text>
  <rect x="110" y="305" width="41.5" height="14" class="f-acc" stroke="currentColor"/>
  <text x="157.5" y="317" fill="currentColor" font-size="12">3458</text>
  <rect x="110" y="322" width="91.0" height="14" class="f-warn" stroke="currentColor"/>
  <text x="207.0" y="334" fill="currentColor" font-size="12">7583</text>
  <line x1="110" y1="36" x2="110" y2="344" stroke="currentColor"/>
  <rect x="110" y="352" width="14" height="12" class="f-ok" stroke="currentColor"/>
  <text x="130" y="363" fill="currentColor" font-size="12">1 B</text>
  <rect x="200" y="352" width="14" height="12" class="f-acc" stroke="currentColor"/>
  <text x="220" y="363" fill="currentColor" font-size="12">4 KB</text>
  <rect x="290" y="352" width="14" height="12" class="f-warn" stroke="currentColor"/>
  <text x="310" y="363" fill="currentColor" font-size="12">64 KB</text>
  <text x="10" y="390" fill="currentColor" font-size="12">작은 메시지: 넷 다 2.4~3.5 µs (FIFO 만 5.6 µs). 64 KB: 버퍼가 8 KB 인 fifo·sock 은 5배 이상 느리다.</text>
</svg>
```

읽는 법:

1. **작은 메시지(1 B~4 KB)는 수단이 거의 상관없다.** 최솟값 2.4~3.5 µs, 크기가 1 B → 4 KB 로 4096배 커져도 +250 ns. 왕복 비용의 대부분은 "블록 → 깨움 → 스위치" 2번이다(OS-M02 의 proc 핑퐁 min 2167 ns 와 같은 크기). 복사 4 KB 는 수십 ns.
2. **shm 이 작은 메시지에서 오히려 느리다 (3167 vs 2375 ns).** 한 방향에 세마포어 연산이 4번(보내는 쪽 `wait(space)`+`post(items)`, 받는 쪽 `wait(items)`+`post(space)`)이고, 이름 있는 세마포어는 블록하지 않을 때도 커널 호출이다. `./bench pair`(한 프로세스, 블록 없음): `sem_post+sem_wait` 515~530 ns, pipe `write+read` 563~607 ns, socket `write+read` 642~693 ns. pipe 는 방향당 시스템 콜 2번, shm 은 4번 → 왕복에서 2 × 2 × ~260 ns ≈ 1 µs 더. 측정값 차이 0.8 µs 와 맞는다. **공유 메모리가 빠른 건 데이터 이동이지 알림이 아니다.** 알림까지 없애려면 바쁜 대기(spin, OS-M02 의 83 ns)나 futex/ulock 같은 "경합 없으면 커널 안 감" 장치가 필요하다(OS-A01).
3. **64 KB 에서는 버퍼 크기가 지배한다.** sock(8 KB) 43 µs, FIFO(8 KB) 39 µs vs sock256k 8.4 µs, pipe(64 KB) 7.3 µs. 버퍼가 8 KB 면 64 KB 를 보내는 동안 보내는 쪽이 8 번 막히고 받는 쪽이 8 번 깨어난다 — 왕복마다 스위치가 16 번 더 생긴다. `SO_SNDBUF`/`SO_RCVBUF` 한 줄로 5배가 바뀐다.
4. **FIFO 는 작은 메시지도 2배 느리다(5.6 µs).** 앞에서 본 대로 XNU 의 FIFO 는 vnode 계층 위에 얹은 소켓 쌍이다. 같은 일을 하는 데 층이 하나 더 있다. (원인을 계측으로 쪼개 보지는 않았다 — "구현이 다르고 실측이 2배" 까지가 확인한 사실.)
5. p99 는 부하 탓에 20~250 µs 까지 퍼졌다. 지연 상한이 중요한 경로라면 평균이 아니라 p99/최대를 보고, 우선순위·코어 배치를 따로 다뤄야 한다(RTOS 트랙, OS-M02 의 QoS 실험).

**대량 전송 — 256 MiB 를 한 방향으로, 수신 측 EOF 후 8 바이트 확인 응답까지 (MB/s, 1 MB = 10⁶ B, 5 run):**

```text
$ ./bench bulk 5 256 64
pipe      median     5731 MB/s   (min     5282, max     7432)
fifo      median      915 MB/s   (min      764, max     1180)
sock      median     1019 MB/s   (min      958, max     1251)
sock256k  median    17153 MB/s   (min    15579, max    18581)
shm+sem   median    23838 MB/s   (min    22721, max    25124)
$ ./bench bulk 5 256 4
pipe      median     4415 MB/s   (min     3849, max     5304)
fifo      median     1126 MB/s   (min      983, max     1288)
sock      median     1365 MB/s   (min      979, max     1405)
sock256k  median     4419 MB/s   (min     3568, max     4689)
shm+sem   median     2806 MB/s   (min     2143, max     3252)
```

```svg
<svg viewBox="0 0 740 380" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">한 방향 256 MiB 전송 처리량 (MB/s, 5 run 중앙값) — 실측</text>
  <text x="10" y="60" fill="currentColor">pipe</text>
  <rect x="110" y="40" width="119.2" height="14" class="f-acc" stroke="currentColor"/>
  <text x="235.2" y="52" fill="currentColor" font-size="12">5731</text>
  <rect x="110" y="57" width="91.8" height="14" class="f-mut" stroke="currentColor"/>
  <text x="207.8" y="69" fill="currentColor" font-size="12">4415</text>
  <text x="10" y="110" fill="currentColor">fifo</text>
  <rect x="110" y="90" width="19.0" height="14" class="f-acc" stroke="currentColor"/>
  <text x="135.0" y="102" fill="currentColor" font-size="12">915</text>
  <rect x="110" y="107" width="23.4" height="14" class="f-mut" stroke="currentColor"/>
  <text x="139.4" y="119" fill="currentColor" font-size="12">1126</text>
  <text x="10" y="160" fill="currentColor">sock</text>
  <rect x="110" y="140" width="21.2" height="14" class="f-acc" stroke="currentColor"/>
  <text x="137.2" y="152" fill="currentColor" font-size="12">1019</text>
  <rect x="110" y="157" width="28.4" height="14" class="f-mut" stroke="currentColor"/>
  <text x="144.4" y="169" fill="currentColor" font-size="12">1365</text>
  <text x="10" y="210" fill="currentColor">sock256k</text>
  <rect x="110" y="190" width="356.8" height="14" class="f-acc" stroke="currentColor"/>
  <text x="472.8" y="202" fill="currentColor" font-size="12">17153</text>
  <rect x="110" y="207" width="91.9" height="14" class="f-mut" stroke="currentColor"/>
  <text x="207.9" y="219" fill="currentColor" font-size="12">4419</text>
  <text x="10" y="260" fill="currentColor">shm+sem</text>
  <rect x="110" y="240" width="495.8" height="14" class="f-acc" stroke="currentColor"/>
  <text x="611.8" y="252" fill="currentColor" font-size="12">23838</text>
  <rect x="110" y="257" width="58.4" height="14" class="f-mut" stroke="currentColor"/>
  <text x="174.4" y="269" fill="currentColor" font-size="12">2806</text>
  <line x1="110" y1="36" x2="110" y2="280" stroke="currentColor"/>
  <rect x="110" y="292" width="14" height="12" class="f-acc" stroke="currentColor"/><text x="130" y="303" fill="currentColor" font-size="12">64 KB 씩 write</text>
  <rect x="250" y="292" width="14" height="12" class="f-mut" stroke="currentColor"/><text x="270" y="303" fill="currentColor" font-size="12">4 KB 씩 write</text>
  <text x="10" y="330" fill="currentColor" font-size="12">큰 조각: shm 23.8 GB/s, 버퍼 키운 소켓 17 GB/s, pipe 5.7 GB/s, 8 KB 버퍼(fifo·sock) ≈ 1 GB/s.</text>
  <text x="10" y="350" fill="currentColor" font-size="12">4 KB 조각: 조각마다 시스템 콜(shm 은 세마포어 4번)이라 전부 1~4.4 GB/s 로 수렴 — 조각 크기가 수단보다 중요.</text>
</svg>
```

1. **큰 조각에서는 shm 이 가장 빠르다(23.8 GB/s)** — 하지만 버퍼만 키운 Unix 소켓(17 GB/s)과 1.4배 차이다. 이 링도 복사를 2번(보내는 쪽 memcpy, 받는 쪽 memcpy) 한다. 생산자가 슬롯 안에 데이터를 **직접 만들면**(카메라 DMA 가 슬롯에 쓰기, 디코더가 슬롯에 출력) 복사가 0~1번으로 줄고 그때 진짜 차이가 난다(ROS-A05, ROS-M06).
2. **pipe 가 sock256k 보다 3배 느리다.** 64 KB 버퍼 = 64 KB 쓰기 한 번마다 거의 매번 막히고 깨운다. macOS 의 pipe 버퍼는 사용자가 키울 수 없다 (리눅스는 `fcntl(F_SETPIPE_SZ)` 로 가능 — `man 7 pipe`, 여기서는 안 재봤다).
3. **4 KB 조각이면 순위가 뒤집힌다.** shm 2.8 GB/s < pipe·sock256k 4.4 GB/s. 4 KB 를 64 KB 슬롯 하나에 담으니 링이 32 KB 밖에 못 쌓고, 조각마다 세마포어 4번. 해결은 수단 교체가 아니라 **배치**(작은 메시지를 모아서 한 슬롯에, OS-M02 Part C) 또는 바이트 단위 링(DS-M10)이다.
4. 8 KB 버퍼(FIFO, 기본 socketpair)는 조각 크기와 상관없이 ~1 GB/s 에 묶인다.

### Part C — 상대가 죽으면

| 수단 | 죽은 상대에게 쓰기 | 읽기 | 이유 |
|---|---|---|---|
| pipe | `EPIPE` (SIGPIPE 무시 시) | 0 = EOF | 커널이 열린 끝의 참조를 센다. 마지막 끝이 닫히면(프로세스가 죽어도 커널이 닫아 준다) 상대가 안다 |
| FIFO | `EPIPE` | EOF | 위와 같음 (이름은 상관없다 — 열린 fd 기준) |
| Unix 소켓 | `EPIPE` | EOF | 위와 같음. 소켓은 `SO_NOSIGPIPE`(macOS) / `MSG_NOSIGNAL`(리눅스)로 시그널만 끌 수도 있다 |
| shm + sem | **성공** (링에 자리가 있으니까) | **영원히 대기** | 공유 메모리와 세마포어는 "누가 붙어 있는지" 를 모른다. 죽은 프로세스가 쥐고 있던 세마포어도 돌려받지 못한다 |

shm 채널을 실제로 쓰려면 상대 사망 감지를 **따로** 만든다: 옆에 Unix 소켓이나 pipe 하나를 두고 EOF 를 감시(가장 흔한 실무 패턴 — 데이터는 shm, 제어·생존은 소켓), 하트비트 카운터 + 타임아웃 대기(리눅스 `sem_timedwait`; **macOS 에는 `sem_timedwait` 가 없다** — 헤더 확인), 또는 robust mutex(OS-M17). 체계적인 설계는 OS-A12.

`pick_ipc()` 의 규칙과 근거 (이 레슨의 실측 + 위 표):

| 순서 | 조건 | 선택 | 근거 |
|---|---|---|---|
| 1 | 메시지 ≥ 1 MiB 또는 ≥ 2000 MB/s | shm (+ 알림·생존용 소켓) | 큰 조각에서만 shm 이 앞선다(23.8 vs 17 vs 5.7 GB/s). 직접 쓰기면 복사 0 |
| 2 | 클라이언트 여럿이 붙었다 떨어짐 | Unix 소켓 `listen`/`accept` | 연결마다 fd, 상대 사망 = EOF, 자격 확인(OS-M16) |
| 3 | 양방향 또는 fd 전달 | 부모-자식이면 `socketpair`, 아니면 Unix 소켓 | fd 하나로 양방향, `SCM_RIGHTS`(OS-M16) |
| 4 | 한 방향 | 부모-자식이면 pipe, 남남이면 FIFO | 가장 단순, 셸과 조합, EOF 로 종료 전파 |

임계값(1 MiB, 2000 MB/s)은 경험칙이다. "그 정도면 복사·시스템 콜에 코어 하나의 상당 부분이 들어간다" 는 뜻이고, 장비의 CPU·메모리 대역폭에 맞게 재야 한다. 메시지 경계·우선순위가 필요하면 POSIX 메시지 큐(OS-M15), 데이터그램 소켓(연습 1)도 후보다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 33/33 passed, 약 0.7 초
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_asan && ./so_asan   # 33/33, 경고 0 (약 2.9 초: fork 마다 ASan 섀도 메모리)
```

## 7. 자주 하는 실수

정답 코드를 한 군데씩 망가뜨려 돌린 실제 결과 (하네스가 모두 잡고, 어느 것도 멈추지 않는다):

| 실수 | 하네스 결과 (실측) | 왜 | 고친 코드 |
|---|---|---|---|
| pipe attach 에서 안 쓰는 끝을 안 닫음 | pipe: `server fdleak`, bulk `killed(sig 14)`, 상대 사망 `ok / HANG` — 28/33 | 서버 자신이 p2c 쓰기 끝을 쥐고 있어 EOF 가 안 온다. 부모가 c2p 읽기 끝을 쥐고 있어 EPIPE 도 안 온다 | 내 몫 2개 외 `close` |
| FIFO 를 자식이 반대 순서로 open | fifo: 전부 `killed(sig 14)` — 28/33 | 부모는 p2c 에서, 자식은 c2p 에서 상대를 기다린다 = 교착 | 양쪽 모두 p2c 먼저 |
| `sem_unlink` 빼먹음 | shm: `names` — 32/33 | 이름이 커널에 남는다 (재부팅 전까지, 연습 4) | 만든 즉시 `sem_unlink` |
| `shm_open` 의 fd 를 안 닫음 | shm: `fdleak` — 32/33 | 매핑은 fd 없이 유지되는데 fd 만 새 나간다 | `mmap` 뒤 `close(fd)` |
| `read_all` 에서 `r == 0` 이면 `continue` | 단위 5개 전부 FAIL, 스트림 3종 `server`/`killed` — 13/33 (워치독 때문에 20 초) | EOF 뒤 `read` 는 계속 0 → 무한 루프 | `if (r == 0) break;` |
| `read_all` 을 `read` 한 번으로 | `U_BIG` FAIL, 스트림 3종 `data` — 26/33 | 짧은 읽기. 200 KB 가 64 KB 로 잘린다 | 루프 |
| `shm_recv` 가 슬롯을 나눠 읽지 않고 남은 부분을 버림 | shm bulk `data` — 32/33 (핑퐁은 통과!) | 핑퐁은 recv 크기가 send 크기와 같아서 안 드러난다. 12345 바이트로 쓰고 4 KB 로 읽는 bulk 에서만 걸린다 | `rx_off` 유지 |
| socketpair 에서 남의 끝을 안 닫음 | sock: `fdleak`, 상대 사망 `ok / HANG` — 31/33 (핑퐁·bulk 데이터는 통과) | 정상 종료에선 `shutdown(SHUT_WR)` 이 사본 개수와 무관하게 EOF 를 보내서 숨는다. 상대가 **죽을 때** 만 드러난다 (연습 5) | `close(c->fd[1 - side])` |

그 밖에:

- **SIGPIPE 를 무시하지 않음** — 죽은 상대에게 쓰는 순간 내 프로세스가 죽는다(기본 동작 = 종료). 데몬은 `signal(SIGPIPE, SIG_IGN)` 하고 `EPIPE` 를 처리한다.
- **양쪽이 동시에 큰 메시지를 보냄** — 핑퐁이 아니라 "둘 다 64 KB 를 먼저 쓰고 나서 읽기" 를 하면 버퍼(8 KB)가 차는 순간 둘 다 `write` 에서 막힌다. 한쪽은 읽기 전용 스레드/`poll` 을 쓰거나 프로토콜을 요청-응답으로 만든다(OS-M12).
- **소켓에서 반쯤 닫기를 `close` 로** — fd 하나가 양방향이라 `close` 하면 응답도 못 받는다. 대량 전송 후 확인 응답을 받으려면 `shutdown(fd, SHUT_WR)` (하네스의 `chan_shutdown_write`).
- **macOS 에서 `sem_init(&s, 1, 0)`** — 컴파일 경고(deprecated)만 나고 실행 시 `-1/ENOSYS`. 반환값을 안 보면 초기화 안 된 세마포어를 쓰게 된다.
- **shm 이름에 `/tmp/...` 같은 경로를 씀** — POSIX 는 `/이름` 하나(슬래시 하나)를 이식 가능한 형태로 정한다. macOS 는 31 바이트 제한.
- **shm 링에 포인터를 넣음** — 두 프로세스의 `mmap` 주소는 다를 수 있다. 링 안에는 오프셋·인덱스만.

## 8. 면접 Q&A

<details>
<summary>Q1. 공유 메모리가 가장 빠른 IPC 아닌가요?</summary>

데이터를 옮기는 부분은 그렇다. 하지만 "언제 읽어도 되나" 를 알리는 비용은 따로 있고, 작은 메시지는 그게 전부다. 이 맥 실측: 1 B 왕복 최솟값 pipe 2375 ns, socketpair 2500 ns, shm+세마포어 3167 ns — shm 이 제일 느렸다. 이름 있는 세마포어 연산이 방향당 4번이고 각각 커널 호출이기 때문이다(`sem_post+sem_wait` 한 쌍 ≈ 520 ns). 큰 데이터(64 KB 조각)에서는 shm 23.8 GB/s, 버퍼 키운 Unix 소켓 17 GB/s, pipe 5.7 GB/s 로 shm 이 앞서고, 생산자가 공유 버퍼에 직접 쓰면 복사가 사라져 차이가 더 벌어진다. 꼬리: "알림 비용도 없애려면?" → 바쁜 대기(지연 최소, CPU 낭비), futex 처럼 경합 없을 땐 사용자 공간에서 끝나는 대기(OS-A01), 또는 배치로 알림 횟수를 줄인다.

</details>

<details>
<summary>Q2. 파이프를 읽는 프로세스가 writer 가 끝났는데도 멈춰 있습니다. 왜죠?</summary>

EOF 는 그 파이프의 **모든** 쓰기 끝이 닫혀야 온다. fork 로 쓰기 끝 사본이 다른 프로세스(대개 읽는 쪽 자신, 혹은 형제 프로세스)에 남아 있으면 커널 입장에서 writer 가 아직 있다. 고침: fork 뒤 각 프로세스는 자기가 안 쓰는 끝을 즉시 닫는다. 이 레슨의 변형 실험에서 attach 의 `close` 를 빼면 서버가 EOF 를 못 봐 워치독에 죽었고, 반대 방향으로는 죽은 상대에게 써도 `EPIPE` 가 안 났다. 꼬리: "어떻게 찾나요?" → `lsof -p <pid>` 로 열린 fd 를 보거나, 리눅스는 `ls -l /proc/<pid>/fd`. `FD_CLOEXEC`(`O_CLOEXEC`, `pipe2`)로 exec 된 자식에게 새는 것도 막는다.

</details>

<details>
<summary>Q3. named FIFO 는 pipe 와 무엇이 다른가요? 함정은?</summary>

파일 시스템에 이름이 있어서 fork 관계가 아닌 프로세스도 `open` 으로 붙을 수 있다. 데이터는 디스크에 안 간다. 함정: ① `open` 이 상대가 열 때까지 막힌다 — 두 FIFO 를 반대 순서로 열면 교착(실측: 워치독에 죽음). `O_NONBLOCK` 이면 읽기 open 은 바로 성공하고, 읽는 쪽 없는 쓰기 open 은 `ENXIO`(연습 3). ② 이름은 프로세스가 죽어도 남는다 → 다 열었으면 `unlink`. ③ writer 가 여럿이면 `PIPE_BUF` 이하 쓰기만 원자적(macOS 512 B, 리눅스 4096 B — 연습 2). ④ macOS 에서는 FIFO 가 내부적으로 AF_LOCAL 소켓 쌍이라 버퍼가 8 KB 이고 pipe 보다 느렸다(1 B 왕복 5.6 vs 2.4 µs, 대량 0.9 vs 5.7 GB/s).

</details>

<details>
<summary>Q4. 상대 프로세스가 죽었다는 걸 IPC 별로 어떻게 알죠?</summary>

pipe·FIFO·소켓: 커널이 fd 참조를 세므로 상대의 마지막 fd 가 닫히면(죽으면 커널이 닫는다) 읽기는 EOF, 쓰기는 `EPIPE`(+ 기본으로 SIGPIPE). `poll` 에서는 `POLLHUP`. 공유 메모리 + 세마포어: 아무것도 안 보인다. 실측에서 shm 쪽 쓰기는 성공했고 읽기는 워치독이 끊을 때까지 기다렸다. 그래서 shm 을 쓰는 시스템은 생존 감지용으로 소켓이나 pipe 를 옆에 두거나(EOF 감시), 하트비트 + 타임아웃, robust mutex(`EOWNERDEAD`, OS-M17), 리눅스면 pidfd(OS-M18)를 쓴다. 꼬리: "죽은 쪽이 세마포어를 잡고 죽었다면?" → 복구 불가. 그래서 크래시 후 재동기화 프로토콜(버전, 시퀀스 번호, 재초기화)이 필요하다(OS-A12).

</details>

<details>
<summary>Q5. Unix 도메인 소켓을 TCP loopback 대신 쓰는 이유는? SOCK_STREAM 과 SOCK_DGRAM 은?</summary>

같은 호스트라면 Unix 소켓은 TCP/IP 스택(체크섬, 혼잡 제어, 라우팅)을 안 거치고, 파일 시스템 권한으로 접근을 제어하고, 상대의 uid/pid 를 확인하고(`getpeereid`, 리눅스 `SO_PEERCRED`), fd 를 넘길 수 있다(`SCM_RIGHTS`) — OS-M16. STREAM 은 경계가 없는 바이트 흐름이라 길이 헤더 등 프레이밍이 필요하고, DGRAM 은 `send` 하나 = `recv` 하나로 경계가 보존된다 (Unix 도메인 DGRAM 은 리눅스·BSD 구현에서 유실·재정렬이 없다 — UDP 와 다른 점, 버퍼가 차면 보내는 쪽이 막히거나 ENOBUFS). 대신 받는 버퍼가 작으면 나머지가 잘려 버려진다(연습 1: 20 바이트 데이터그램을 5 바이트로 받으면 15 바이트 사라짐). 꼬리: "TCP 처럼 Nagle 지연은?" → Unix 소켓에는 없다.

</details>

<details>
<summary>Q6. 이 측정 결과를 어떻게 믿을 수 있죠? 벤치마크 함정은?</summary>

① 측정 중 load average 가 13~42 였다 — 그래서 중앙값·p99 는 크게 흔들렸고, 최솟값과 수단 간 비율만 결론에 썼다. ② 워밍업(처음 300 왕복 버림), run 5번의 중앙값. ③ macOS 는 코어 고정이 안 돼서 같은 코어/다른 코어 깨움이 섞인다(OS-M02). ④ 대량 전송은 보내는 쪽 버퍼를 미리 채워 두고 "IPC 만" 쟀다 — 실제 앱은 데이터를 만들고 쓰는 비용이 더해진다. ⑤ 조각 크기·버퍼 크기가 수단보다 더 큰 변수였다(64 KB vs 4 KB 조각에서 shm 순위가 뒤집힘). 그래서 "X 가 빠르다" 가 아니라 "이 크기·이 버퍼에서 X 가 Y 배" 로 말한다. ⑥ 정확성 테스트(체크섬·순서)와 측정 코드를 분리했다 — 체크섬 계산이 벤치 숫자를 오염시키지 않도록.

</details>

<details>
<summary>Q7. 임베디드 리눅스 장비에서 실제로는 어떻게 고르나요?</summary>

한 방향 스트림(센서 데몬 → 로거)은 pipe/FIFO, 서비스형 데몬(설정, 업데이트 에이전트, 워치독)은 Unix 소켓 — systemd 의 소켓 활성화, `sd_notify` 의 `WATCHDOG=1` 도 Unix 데이터그램 소켓이다(ELX-B13). D-Bus 도 Unix 소켓 위에서 돈다(BMC-M04). 카메라 프레임·레이더 샘플처럼 큰 데이터는 shm(혹은 dma-buf 같은 커널 버퍼 공유) + 알림용 소켓/eventfd(OS-M18). 메시지 단위·우선순위가 필요하면 POSIX mq(OS-M15). MMU 없는 Cortex-M + RTOS 에서는 모두가 같은 주소 공간이라 "공유 메모리" 가 기본이고, 문제는 오직 동기화다 — 큐(RTOS-B11), 태스크 알림(RTOS-M01), ISR↔태스크 링(DS-A02). 멀티코어 SoC 의 코어 간 통신은 공유 SRAM 링 + 메일박스 인터럽트(PLAT-M05) — 이 레슨의 `items` 세마포어가 하드웨어 doorbell 로 바뀐 모양이다.

</details>

## 9. 추가 연습

### 연습 1 — 메시지 경계: STREAM vs DGRAM

socketpair 를 `SOCK_STREAM`, `SOCK_DGRAM` 으로 하나씩 만들고 10, 20, 30 바이트를 연속으로 보낸다. STREAM 은 100 바이트 `recv` 한 번에 무엇이 오나? DGRAM 에서 20 바이트 메시지를 5 바이트 버퍼로 받으면?

<details>
<summary>답 보기</summary>

STREAM: 60 바이트가 한 번에 온다 — 경계가 없다(그래서 하네스는 `{magic, seq, len, sum}` 헤더로 프레이밍). DGRAM: `recv` 3번이 10, 5(잘림), 30 을 준다. 잘린 15 바이트는 버려지고 다음 `recv` 는 다음 데이터그램이다. 잘렸는지 알려면 `recvmsg` 의 `MSG_TRUNC` 플래그를 본다. 실측: `ex1 stream: one recv(100) -> 60 bytes`, `ex1 dgram : recv -> 10, 5 (truncated), 30`.

</details>

### 연습 2 — PIPE_BUF 원자성

writer 프로세스 4개가 같은 파이프에 `PIPE_BUF` 바이트짜리 레코드(자기 글자로 채움)를 500개씩 쓴다. reader 가 받은 레코드 중 섞인 것이 있나?

<details>
<summary>답 보기</summary>

없다. POSIX `write`: 파이프에 `PIPE_BUF` 이하를 쓰면 다른 writer 의 데이터와 섞이지 않는다(원자적). 그보다 크면 섞일 수 있다. macOS 의 `PIPE_BUF` 는 **512** (리눅스 4096). 실측: `PIPE_BUF=512: 1024000 bytes, mixed records 0, per writer 500 500 500 500`. 로그 데몬 하나에 여러 프로세스가 FIFO 로 한 줄씩 보내는 설계가 이 보장에 기댄다 — 한 줄이 512 B 를 넘으면 macOS 에서는 보장이 없다. 부모는 자기 쓰기 끝을 닫아야 writer 가 다 끝났을 때 EOF 를 본다(Q2).

</details>

### 연습 3 — FIFO 열기 규칙과 버퍼 용량

O_NONBLOCK 으로 ① 읽는 쪽 없이 쓰기 open ② 쓰는 쪽 없이 읽기 open 을 해 보고, pipe / FIFO / socketpair 기본 / `SO_SNDBUF` 256 KB socketpair 에 막힐 때까지(EAGAIN) 1 바이트씩 써서 용량을 재라.

<details>
<summary>답 보기</summary>

① `-1 / ENXIO` (막히는 대신 실패) ② 바로 성공. 용량 실측: `pipe 65536, fifo 8192, socketpair 8192, socketpair(256K) 262144`. FIFO 가 pipe 가 아니라 소켓과 같은 8 KB 인 것이 XNU 구현(AF_LOCAL 소켓 쌍)과 맞는다. 체크는 POSIX 가 보장하는 것(`>= PIPE_BUF`)과 "키우면 실제로 4배 넘게 담긴다" 만 본다. 이 용량이 RTT 표의 64 KB 행과 bulk 표의 차이를 설명한다.

</details>

### 연습 4 — 이름은 프로세스보다 오래 산다

자식이 `shm_open` + `ftruncate` + `mmap` 으로 42 를 쓰고 `sem_open(값 3)` 한 뒤 unlink 없이 `SIGKILL` 로 죽는다. 부모가 같은 이름으로 열면?

<details>
<summary>답 보기</summary>

열린다. shm 내용(42)도, 세마포어 값(3)도 그대로다 — POSIX shm·sem 은 커널 객체이고 이름은 `unlink` 하거나 재부팅할 때까지 남는다. 실측: `after SIGKILL: shm left=yes value=42, sem left=yes`. 부모가 `shm_unlink`/`sem_unlink` 하면 그 뒤 `shm_open` 은 `ENOENT`. 그래서 본문은 fork 로 물려줄 때 **만든 즉시 unlink** 하고, 남남 프로세스는 시작할 때 이전 실행이 남긴 이름을 지우고(`O_CREAT|O_EXCL` 실패 시 정리) 핸드셰이크 뒤에 unlink 한다. 리눅스에서는 `/dev/shm/` 아래 파일로 보여서 `ls` 로 누수를 찾을 수 있다.

</details>

### 연습 5 — close vs shutdown

socketpair 의 한쪽 fd 를 `dup` 한 뒤 원본만 `close` 하면 상대가 EOF 를 보나? 사본에 `shutdown(SHUT_WR)` 을 하면?

<details>
<summary>답 보기</summary>

`close` 는 fd 참조 하나만 줄인다 — 사본이 남아 있으면 소켓은 살아 있고 상대의 non-blocking `read` 는 `EAGAIN`. `shutdown` 은 **소켓 자체** 에 "더 안 보냄" 을 기록하므로 사본 개수와 무관하게 상대가 EOF(0)를 본다. 실측: `after close(one copy): read=-1 (EAGAIN); after shutdown(SHUT_WR): read=0`. 7. 실수 표의 "socketpair 에서 남의 끝을 안 닫음" 이 정상 종료에선 통과하고 상대 사망 테스트에서만 걸린 이유가 이것이다 — 정상 종료는 `shutdown` 을 거치지만 `SIGKILL` 은 `close` 만 일어난다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 14/14 passed, 0.25 초
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_asan && ./pr_asan   # 14/14, 경고 0
```

## 10. 핵심 정리

- 작은 메시지의 IPC 비용은 "블록 → 깨움 → 스위치" 가 전부다: pipe·socket·shm 모두 1 B 왕복 2.4~3.2 µs. **shm+세마포어가 오히려 느렸다**(방향당 커널 호출 4번).
- 큰 데이터는 **버퍼 크기와 복사 횟수**: 64 KB 조각 shm 23.8 GB/s, socket(256 KB 버퍼) 17 GB/s, pipe(64 KB) 5.7 GB/s, 8 KB 버퍼(FIFO·기본 socketpair) ~1 GB/s. 4 KB 조각이면 전부 1~4.4 GB/s — 배치가 수단보다 중요.
- 스트림 3원칙: `read` 는 짧게 온다 → `read_all` 루프 / `read == 0` 은 EOF → 멈춤 / fork 뒤 안 쓰는 끝은 닫는다 → 그래야 EOF·EPIPE 가 온다.
- FIFO: `open` 이 상대를 기다린다 → 양쪽 같은 순서. macOS FIFO = 내부 소켓 쌍(8 KB, pipe 보다 느림).
- 이름(FIFO 경로, shm·sem)은 프로세스보다 오래 산다 → 만든 즉시(상속) 또는 핸드셰이크 뒤 unlink. macOS: `sem_init` 없음, `sem_timedwait` 없음, shm 이름 31 B, `ftruncate` 한 번.
- 상대 사망: fd 기반은 EOF/EPIPE 로 알려 주고, shm 은 아무것도 안 알려 준다 → 생존 감지용 소켓·하트비트를 같이.
- 선택: 한 방향 → pipe/FIFO, 양방향·fd 전달 → socketpair/Unix 소켓, 여러 클라이언트 → Unix 소켓 서버, 대용량 → shm + 제어용 소켓. 메시지 경계 → DGRAM/mq.

## 참고자료

- POSIX `pipe`, `write`(PIPE_BUF 원자성, EPIPE/SIGPIPE), `read`: <https://pubs.opengroup.org/onlinepubs/9699919799/functions/pipe.html>, <https://pubs.opengroup.org/onlinepubs/9699919799/functions/write.html>
- POSIX `mkfifo`, `open`(FIFO 와 O_NONBLOCK, ENXIO): <https://pubs.opengroup.org/onlinepubs/9699919799/functions/mkfifo.html>, <https://pubs.opengroup.org/onlinepubs/9699919799/functions/open.html>
- POSIX `socketpair`, `shutdown`: <https://pubs.opengroup.org/onlinepubs/9699919799/functions/socketpair.html>, <https://pubs.opengroup.org/onlinepubs/9699919799/functions/shutdown.html>
- POSIX `shm_open`, `shm_unlink`, `sem_open`, `sem_unlink`: <https://pubs.opengroup.org/onlinepubs/9699919799/functions/shm_open.html>, <https://pubs.opengroup.org/onlinepubs/9699919799/functions/sem_open.html>
- POSIX `fork` (열린 세마포어·매핑 상속, 타이머 비상속): <https://pubs.opengroup.org/onlinepubs/9699919799/functions/fork.html>
- POSIX XBD 4.12 Memory Synchronization (sem_post/sem_wait 가 메모리 동기화): <https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap04.html#tag_04_12>
- XNU 소스 `bsd/miscfs/fifofs/fifo_vnops.c` — `fifo_open` 의 `socreate(AF_LOCAL, ..., SOCK_STREAM)` + `soconnect2`: <https://github.com/apple-oss-distributions/xnu/blob/main/bsd/miscfs/fifofs/fifo_vnops.c>
- macOS SDK 헤더: `sys/semaphore.h` (`sem_init` `__deprecated`, `sem_timedwait` 없음), `sys/posix_shm.h` (`PSHMNAMLEN 31`), `sys/syslimits.h` (`PIPE_BUF 512`), `sys/pipe.h` (`BIG_PIPE_SIZE 64*1024`)
- Linux man pages `pipe(7)` (용량, F_SETPIPE_SZ), `unix(7)`, `shm_overview(7)`, `sem_overview(7)` — 리눅스 비교용(이 레슨에서 리눅스는 재지 않음)
- OSTEP 5장 *Process API* (5.4 pipe), 31장 *Semaphores* (31.4 bounded buffer, 그림 31.12), 39장 *Files and Directories* (39.9 unlink, 39.13 hard links — 이름과 객체의 분리) — 로컬: `operating-system-three-easy-pieces/book-md/C05_interlude_process_api.md`, `C31_semaphores.md`, `C39_interlude_files_and_directories.md`
- W. R. Stevens & S. Rago, *Advanced Programming in the UNIX Environment* 3판 15장 (IPC), 17장 (Advanced IPC)
- 관련 레슨: OS-B02 (fd·dup2·파이프 EOF 행), OS-B07 (SIGPIPE·EINTR), OS-B09 (mmap), OS-M02 (핑퐁 컨텍스트 스위치), OS-M12 (I/O 모델), OS-M15 (POSIX mq), OS-M16 (SCM_RIGHTS·피어 자격), OS-M17 (프로세스 공유 mutex), OS-M18 (eventfd·pidfd), OS-A01 (futex), OS-A12 (견고한 shm IPC), OS-A14 (Mach 메시지·XPC), DS-A01 (lock-free SPSC), DS-M10 (가변 길이 레코드 링), DS-A02 (ISR↔태스크 링), RTOS-B11 (메시지 큐), RTOS-M01 (태스크 알림), PLAT-M05 (멀티코어 메일박스), ROS-A05·ROS-M06 (shm·zero-copy 전송), ELX-B13 (systemd watchdog), BMC-M04 (D-Bus)
