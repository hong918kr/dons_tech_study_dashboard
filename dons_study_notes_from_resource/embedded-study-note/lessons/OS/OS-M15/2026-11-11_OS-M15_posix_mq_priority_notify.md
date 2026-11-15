---
id: OS-M15
time: 90분
updated: 2026-11-11
---
# OS-M15 POSIX 메시지 큐: mq_open·우선순위·mq_notify

## 학습 목표

- `mq_open / mq_send / mq_receive` 로 생산자 2 (fork) → 소비자 1 파이프라인을 만들고, 커널이 **메시지 경계**와 **같은 우선순위 안의 FIFO** 를 지켜 준다는 것을 체크섬·순서 검사로 보인다.
- 우선순위 순서를 "안정 정렬(내림차순)" 모델로 예측하고 무작위 300 회에서 커널과 1:1 로 맞는지 확인한다. 가득 찬 큐에서 기본 모드는 **막히고**, `O_NONBLOCK` 은 `EAGAIN`, `mq_timedsend/receive` 는 `ETIMEDOUT` 임을 테스트로 보인다.
- `mq_timedreceive` 의 시간 인자가 **CLOCK_REALTIME 기준 절대 시각** 이라는 것, 그리고 상대 시간을 넘기면 왜 즉시 실패하는지 설명한다.
- `mq_notify` 의 규칙 네 가지(1회용, 빈 큐 → 비어 있지 않음일 때만, 한 큐에 한 프로세스(EBUSY), 막힌 수신자가 있으면 무시)를 SIGEV_SIGNAL·SIGEV_THREAD 로 확인하고 "**재등록 먼저, 비우기는 나중**" 의 이유를 말한다.
- `/proc/sys/fs/mqueue` 한도와 `RLIMIT_MSGQUEUE` 가 각각 어떤 errno(EINVAL·EMFILE·ENOSPC) 로 터지는지, mq 의 지연이 pipe·Unix 소켓과 비교해 어떤지 VM 실측 숫자로 말한다.

## 1. 문제

"pipe, Unix 소켓, POSIX 메시지 큐 중 무엇을 쓰나요? mq 의 장점은?" — 임베디드 리눅스 면접에서 IPC 다음에 꼭 나오는 질문이다. 답의 핵심은 **mq 는 커널이 메시지 경계와 우선순위를 관리해 준다** 는 것이고, 꼬리질문은 거의 항상 함정 쪽이다: 타임아웃이 절대 시각인 것, 수신 버퍼가 `mq_msgsize` 보다 작으면 1 바이트 메시지도 `EMSGSIZE` 인 것, `mq_notify` 가 1회용인 것, 비특권 프로세스는 큐 깊이가 기본 10 개로 묶이는 것. 펌웨어 쪽에서는 RTOS 큐(RTOS-B11, RTOS-M06)와 거의 같은 모양이라서 "리눅스로 올라간 RTOS 코드의 큐를 무엇으로 바꾸나" 에 대한 가장 직접적인 답이기도 하다. 예: i.MX8 보드의 센서 데몬에서 IMU·GPS 수집 프로세스(생산자)가 로거 프로세스(소비자)로 샘플을 보내고, 결함 감시 프로세스는 **우선순위 31** 경보를 같은 큐에 넣어 줄을 건너뛰게 한다.

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">생산자 2 → 커널 큐(/dev/mqueue/OS-M15-pid-pipe) → 소비자 1</text>
  <rect x="10" y="40" width="150" height="50" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="20" y="62" fill="currentColor">생산자 0 (fork)</text>
  <text x="20" y="80" fill="currentColor" font-size="12">mq_send(prio 0) ×500</text>
  <rect x="10" y="110" width="150" height="50" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="20" y="132" fill="currentColor">생산자 1 (fork)</text>
  <text x="20" y="150" fill="currentColor" font-size="12">mq_send(prio 0) ×500</text>
  <rect x="10" y="180" width="150" height="50" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="20" y="202" fill="currentColor">결함 감시</text>
  <text x="20" y="220" fill="currentColor" font-size="12">mq_send(prio 31)</text>
  <rect x="220" y="40" width="270" height="190" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="232" y="60" fill="currentColor" font-weight="bold">커널: 우선순위별 FIFO 묶음</text>
  <rect x="235" y="72" width="240" height="30" class="f-warn" stroke="currentColor"/>
  <text x="245" y="92" fill="currentColor" font-family="monospace" font-size="12">prio 31: [alarm]</text>
  <rect x="235" y="110" width="240" height="30" class="f-ok" stroke="currentColor"/>
  <text x="245" y="130" fill="currentColor" font-family="monospace" font-size="12">prio 0 : [p0#7][p1#4][p0#8]</text>
  <text x="235" y="162" fill="currentColor" font-size="12">maxmsg 4 · msgsize 64 (비특권 상한 10)</text>
  <text x="235" y="182" fill="currentColor" font-size="12">가득 → send 막힘 / O_NONBLOCK: EAGAIN</text>
  <text x="235" y="202" fill="currentColor" font-size="12">메시지 하나 = 한 번의 send (경계 유지)</text>
  <text x="235" y="222" fill="currentColor" font-size="12">receive = 가장 높은 prio 의 가장 오래된 것</text>
  <rect x="550" y="80" width="180" height="70" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="560" y="102" fill="currentColor">소비자 (부모)</text>
  <text x="560" y="122" fill="currentColor" font-size="12">mq_timedreceive</text>
  <text x="560" y="140" fill="currentColor" font-size="12">crc · 생산자별 seq 검사</text>
  <rect x="550" y="170" width="180" height="60" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="560" y="192" fill="currentColor" font-size="12">깨우는 길 3가지:</text>
  <text x="560" y="210" fill="currentColor" font-size="12">막힌 receive · poll(mqd)</text>
  <text x="560" y="226" fill="currentColor" font-size="12">mq_notify(신호/스레드)</text>
  <g stroke="currentColor" fill="none">
    <path d="M160 65 L218 90"/><path d="M160 135 L218 130"/><path d="M160 205 L218 170"/>
    <path d="M490 115 L548 115"/><path d="M490 200 L548 200" stroke-dasharray="4 3"/>
  </g>
  <text x="10" y="262" fill="currentColor">mqd_t 는 리눅스에서 그냥 fd → poll/epoll 가능 (이식성 없음, mq_overview(7)).</text>
  <text x="10" y="284" fill="currentColor">macOS 에는 &lt;mqueue.h&gt; 자체가 없다 → 이 레슨은 Lima VM elinux 에서만 돈다.</text>
  <text x="10" y="304" fill="currentColor">이름은 "/OS-M15-&lt;pid&gt;-&lt;tag&gt;" → 끝나면 mq_unlink (파일처럼 이름이 프로세스보다 오래 산다).</text>
</svg>
```

**환경 (이 레슨의 모든 숫자)**: Lima VM `elinux` (vz, aarch64, 6 vCPU) · Ubuntu 26.04.1 LTS · kernel `7.0.0-38-generic` · glibc 2.43 (`ldd --version` → `ldd (Ubuntu GLIBC 2.43-2ubuntu2.4) 2.43`) · `gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0` · `Ubuntu clang version 21.1.8`. 비특권 사용자(uid 501). 호스트: Apple M6, macOS 27.2, `Apple clang version 21.0.0`. glibc 2.34 부터 `mq_*` 가 libc 안으로 들어와서(`nm -D libc.so.6` 에 `mq_open@@GLIBC_2.34`) **`-lrt` 가 필요 없다**. `-pthread` 는 SIGEV_THREAD 콜백 때문에 붙인다(`/* BUILD: -pthread */`).

맥에서 시도하면 (실제 출력):

```text
$ cc -std=c11 mac_mq.c        # #include <mqueue.h> 한 줄
mac_mq.c:1:10: fatal error: 'mqueue.h' file not found
```

### Part A — 생산자 2 → 소비자 1, 경계와 이름

- 주어진 것: `q_new(tag, maxmsg, msgsize, flags)` (O_CREAT|O_EXCL, 이름 기록 → atexit 에서 mq_unlink), `msg_t` 64 B {prod, seq, crc, payload}, `producer_main` (막히는 `mq_send` ×500, prio 0).
- **TODO** `consume_verify(q, nprod, total, ms, &r)` — `recv_timeout` 으로 total 개 받으며 길이·crc·생산자별 seq 를 검사. 큐 깊이 4 라 생산자가 계속 막혔다 풀린다.
- 테스트: 1000 개 전부, 손상 0, 생산자별 순서 어김 0, 생산자 exit 0, 끝나면 큐 0 개. 1/100/8192 B 를 보내면 받는 쪽도 정확히 1/100/8192. `mq_unlink` 뒤 이름으로는 못 열지만(ENOENT) 열린 디스크립터는 계속 동작.

### Part B — 우선순위·가득 참·타임아웃·속성·한도

- **TODO** `prio_model(prio, n, order)` — 받는 순서 예측(높은 prio 먼저, 같으면 먼저 보낸 것). 무작위 300 회(절반은 prio 0..3 이라 동점 투성이)에서 커널과 비교.
- **TODO** `try_send_nb` — `mq_setattr` 로 이번 한 번만 O_NONBLOCK, 원래 플래그 복구. 가득 차면 `-EAGAIN`.
- **TODO** `abs_deadline_ms` + `recv_timeout` — CLOCK_REALTIME 절대 마감, EINTR 이면 같은 마감으로 재시도.
- **TODO** `read_mq_limit("msg_max")` — `/proc/sys/fs/mqueue/*` 읽기 → 한도 = 허용, 한도+1 = EINVAL.
- 주어진 테스트: 기본 모드 send 는 꽉 찬 큐에서 막힘(자식 프로세스가 150 ms 동안 못 끝냄 → 하나 받아 주면 끝남), `mq_timedsend` ETIMEDOUT, `mq_setattr` 는 `mq_flags` 만 바꿈, EMSGSIZE 두 방향, prio = MQ_PRIO_MAX → EINVAL.

### Part C — mq_notify 와 이벤트 루프

- **TODO** `notify_arm_signal(q, signo, value)` — SIGEV_SIGNAL. 신호의 `si_code == SI_MESGQ`, `si_pid` = 보낸 프로세스.
- **TODO** `notify_arm_thread` + `on_notify` — SIGEV_THREAD 소비자: 콜백은 **① 재등록 ② O_NONBLOCK 디스크립터로 EAGAIN 까지 비우기**.
- 테스트: 1회용(재등록 없으면 두 번째 신호 없음), 비어 있지 않을 때 등록하면 비운 뒤 들어올 때까지 안 옴, 다른 프로세스가 등록 중이면 `-EBUSY` → 그 프로세스가 끝나면 등록 가능, `poll(mqd)` 의 POLLIN/POLLOUT, 5 라운드 9 개를 콜백이 빠짐없이 받음.

## 2. Problem (English)

> On Linux, build a 2-producer / 1-consumer pipeline on a POSIX message queue and characterise its semantics.
>
> ```c
> int     consume_verify(mqd_t q, int nprod, int total, long ms, cres_t *r); // 0 or -1 on timeout
> void    prio_model(const unsigned *prio, int n, int *order);              // predicted receive order
> int     try_send_nb(mqd_t q, const void *buf, size_t len, unsigned prio); // 0 or -errno, flags restored
> void    abs_deadline_ms(long ms, struct timespec *ts);                    // CLOCK_REALTIME now + ms
> ssize_t recv_timeout(mqd_t q, void *buf, size_t cap, unsigned *prio, long ms); // bytes or -errno
> long    read_mq_limit(const char *name);                                  // /proc/sys/fs/mqueue/<name>
> int     notify_arm_signal(mqd_t q, int signo, int value);                 // 0 or -errno (-EBUSY)
> int     notify_arm_thread(tctx_t *c);  void on_notify(union sigval sv);   // SIGEV_THREAD drainer
> ```
>
> Constraints: queue depth 4 so producers block; every message carries (producer, seq, crc) and the consumer
> must detect loss, corruption and per-producer reordering; timeouts never hang the test; queue names are
> unique per pid and unlinked at exit. Show: equal-priority FIFO, higher priority first, `EAGAIN` vs blocking on a
> full queue, `ETIMEDOUT`, `EMSGSIZE` when the receive buffer is smaller than `mq_msgsize`, `EINVAL` above
> `/proc/sys/fs/mqueue` limits, one-shot notification only on the empty → non-empty transition, `EBUSY` for a
> second process. Then compare round-trip latency with a pipe and a Unix socket.
> Example: priorities sent `{1, 5, 3, 5, 0}` → received indices `{1, 3, 2, 0, 4}`.
> Follow-ups: why is the timeout absolute? Why re-arm before draining? Where does `mq_notify(SIGEV_THREAD)`
> get its thread from in glibc? What happens to a notification when a reader is blocked in `mq_receive`?

## 3. 시작 코드

<!-- code: starter.c -->

**리눅스 전용** (`/* ENV: linux */`, 맥 검증기는 건너뛴다). 맥 홈은 VM 에 읽기 전용이라 VM 의 `/tmp/OS-M15/` 로 복사해서 빌드한다.

```bash
limactl list                                      # elinux 가 Running 인지
limactl shell elinux                              # 이후 VM 안
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/OS/OS-M15
mkdir -p /tmp/OS-M15 && cp $L/*.c /tmp/OS-M15/ && cd /tmp/OS-M15
gcc   -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o st && ./st
clang -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o st && ./st
ls /dev/mqueue                                    # 끝난 뒤 비어 있어야 한다
```

시작 코드 실행 (실제 출력 일부, 2.8 s — 모든 대기에 상한이 있고 막힌 생산자는 `reap()` 이 SIGKILL 한다):

```text
== Part A: 생산자 2 (fork) → 소비자 1 ==
  got 0  bad_len 0  bad_crc 0  out_of_order 0  next = {0, 0}  producers exit -1 -1
FAIL  rc == 0 && r.got == 2 * PER  (line 278)
...
== Part B: 커널 순서 == 모델 순서 (무작위 300 회, 동점 많음) ==
  300 회 중 순서 불일치 1686, 우선순위 불일치 1686
FAIL  mism == 0 && prio_mism == 0  (line 348)
...
  빈 큐, 30 ms → -110 (Connection timed out), 0 ms 걸림
  50 ms 뒤 도착, 3000 ms 대기 → -110 (prio 0), 0 ms 걸림
...
21/51 passed
```

PASS 21 개는 TODO 와 무관한 커널 동작 테스트(경계 밖 prio = EINVAL, 막히는 send, EMSGSIZE, poll …)다. "0 ms 걸림" 두 줄이 Part B 의 핵심 버그 모양 — 마감 시각이 1970 년이면 기다리지 않는다.

## 4. 힌트

<details>
<summary>힌트 1 — consume_verify: 버퍼 크기와 seq 표</summary>

수신 버퍼는 **큐의 `mq_msgsize` 이상** 이어야 한다(메시지 길이가 아니라). `next[prod]` 에 "다음 기대 seq" 를 두고, 다르면 out_of_order++ 후 `next = seq + 1` 로 맞춰서 한 번 어긋난 것을 계속 세지 않게 한다. 길이를 먼저 보고 `memcpy` 로 `msg_t` 에 옮긴다(정렬 걱정 없음).

</details>

<details>
<summary>힌트 2 — prio_model: 안정 정렬</summary>

삽입 정렬로 `order[]` 를 만든다. 비교는 `prio[order[j-1]] < prio[i]` 일 때만 밀어낸다 — `<=` 로 쓰면 같은 우선순위끼리 순서가 뒤집혀 "같은 prio 는 FIFO" 가 깨진다.

</details>

<details>
<summary>힌트 3 — try_send_nb: 바꿀 수 있는 속성은 하나</summary>

`mq_setattr(q, &new, &old)` 는 `mq_flags` 의 `O_NONBLOCK` 만 본다(maxmsg/msgsize 는 무시). 보낸 결과의 `errno` 를 **복구 호출 전에** `-errno` 로 잡아 두고, `old` 로 되돌린다.

</details>

<details>
<summary>힌트 4 — 절대 마감</summary>

`clock_gettime(CLOCK_REALTIME, ts)` 에 `ms/1000` 초와 `(ms%1000)*1e6` ns 를 더하고 `tv_nsec >= 1e9` 면 올림. `mq_timedreceive` 가 `EINTR` 이면 **같은 `ts`** 로 다시 부른다 — 새로 계산하면 신호가 올 때마다 마감이 뒤로 밀린다.

</details>

<details>
<summary>힌트 5 — 알림 등록과 콜백</summary>

`struct sigevent` 를 0 으로 채우고 `sigev_notify = SIGEV_SIGNAL`, `sigev_signo`, `sigev_value.sival_int`. 스레드면 `SIGEV_THREAD`, `sigev_notify_function`, `sigev_value.sival_ptr = c`. 콜백은 큐가 아직 비어 있지 않을 때 재등록해야 "비운 직후 들어온 메시지" 를 놓치지 않는다 (man mq_notify: "This should be done before emptying all unread messages").

</details>

## 5. 풀이

### 0. mq 계약 — 이 레슨에 필요한 만큼

| 호출 | 성공 | 이 레슨에서 보는 실패 |
|---|---|---|
| `mq_open(name, O_CREAT\|O_EXCL\|O_RDWR, 0600, &attr)` | mqd_t (리눅스: fd) | EEXIST, EINVAL(한도 초과·maxmsg 0), EMFILE(RLIMIT_MSGQUEUE), ENOSPC(queues_max) |
| `mq_send(q, buf, len, prio)` | 0 | 가득 참: 막힘 / EAGAIN(O_NONBLOCK), EMSGSIZE(len &gt; msgsize), EINVAL(prio ≥ MQ_PRIO_MAX) |
| `mq_receive(q, buf, cap, &prio)` | 바이트 수 | EMSGSIZE(cap &lt; msgsize), 비었음: 막힘 / EAGAIN |
| `mq_timed{send,receive}(…, &abs)` | 위와 같음 | ETIMEDOUT, EINVAL(tv_nsec 범위 밖) |
| `mq_getattr / mq_setattr` | 0 | setattr 은 `mq_flags`(O_NONBLOCK)만 반영 |
| `mq_notify(q, &sev)` / `(q, NULL)` | 0 | EBUSY(다른 프로세스가 등록 중) |
| `mq_unlink(name)` | 0 | ENOENT. 이름만 지우고, 열린 큐는 마지막 close 때 사라진다 |

`sysconf(_SC_MQ_PRIO_MAX)` = **32768** (이 VM). POSIX 최소는 32 라서 이식성을 원하면 0..31 만 쓴다.

### Part A — 소비자와 경계

```c
while (r->got < total) {
    ssize_t n = recv_timeout(q, buf, sizeof buf, &prio, ms);   /* buf 128 ≥ msgsize 64 */
    if (n < 0) { r->timeouts++; return -1; }
    r->got++;
    if (n != (ssize_t)sizeof(msg_t)) { r->bad_len++; continue; }
    ...
    if (m.seq != r->next[m.prod]) r->out_of_order++;
    r->next[m.prod] = m.seq + 1;
}
```

두 생산자가 섞여 들어오므로 **전체 순서**는 실행마다 다르다. 하지만 같은 생산자의 메시지는 같은 우선순위 0 이고 그 생산자가 순서대로 보냈으니 커널이 FIFO 로 지킨다 → 생산자별 seq 는 반드시 0,1,2,… 로 이어진다. 이것이 결정적으로 검사할 수 있는 성질이다.

| 성질 | pipe / SOCK_STREAM | SOCK_SEQPACKET / DGRAM | POSIX mq |
|---|---|---|---|
| 메시지 경계 | 없음 (read 가 합치거나 자름) | 있음 | 있음 |
| 우선순위 | 없음 | 없음 (MSG_OOB 제외) | 0..MQ_PRIO_MAX-1 |
| 이름 | 없음 (FIFO 는 경로) | 경로/추상 이름 | `/name` (mqueue fs) |
| 큐 깊이 | 64 KiB 버퍼 (바이트) | 소켓 버퍼 | **메시지 개수** (비특권 ≤ 10) |
| 여러 생산자 | PIPE_BUF 이하만 원자적 | 메시지 단위 | 메시지 단위 |
| 수명 | 마지막 close | 마지막 close | **mq_unlink + 마지막 close** |

`mq_unlink` 테스트의 실제 결과: 이름을 지운 뒤 `mq_open(name, O_RDWR)` → `ENOENT`, 하지만 `mq_send(b,"hi")` / `mq_receive` 는 성공. 파일 `unlink` 와 같은 모델이다. 그래서 이 레슨은 `atexit` 에서 이름을 지운다. 비정상 종료로 남은 큐는 `ls /dev/mqueue` 로 보이고 `rm` 으로 지울 수 있다(리눅스는 mqueue 파일시스템을 `/dev/mqueue` 에 마운트한다).

### Part B — 우선순위 순서

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">보낸 순서 i:prio = 0:1, 1:5, 2:3, 3:5, 4:0  →  받는 순서 {1, 3, 2, 0, 4}</text>
  <rect x="20" y="40" width="300" height="34" class="f-warn" stroke="currentColor"/>
  <text x="30" y="62" fill="currentColor" font-family="monospace" font-size="12">prio 5 : [i=1] → [i=3]</text>
  <rect x="20" y="80" width="300" height="34" class="f-acc" stroke="currentColor"/>
  <text x="30" y="102" fill="currentColor" font-family="monospace" font-size="12">prio 3 : [i=2]</text>
  <rect x="20" y="120" width="300" height="34" class="f-ok" stroke="currentColor"/>
  <text x="30" y="142" fill="currentColor" font-family="monospace" font-size="12">prio 1 : [i=0]</text>
  <rect x="20" y="160" width="300" height="34" class="f-mut" stroke="currentColor"/>
  <text x="30" y="182" fill="currentColor" font-family="monospace" font-size="12">prio 0 : [i=4]</text>
  <path d="M330 57 L380 57 L380 177" stroke="currentColor" fill="none"/>
  <text x="390" y="62" fill="currentColor">receive 는 항상 맨 위 줄의 맨 앞</text>
  <text x="390" y="86" fill="currentColor">= (prio 내림차순, 보낸 순서) 안정 정렬</text>
  <text x="390" y="110" fill="currentColor">커널(3.5+): prio 마다 노드 하나인 rbtree,</text>
  <text x="390" y="130" fill="currentColor">노드 안은 FIFO 리스트 (ipc/mqueue.c)</text>
  <text x="390" y="160" fill="currentColor">모델 = 삽입 정렬, 비교는 '&lt;' (≤ 면 FIFO 깨짐)</text>
  <text x="20" y="225" fill="currentColor">무작위 300 회 (n ≤ 10, prio 0..31 / 0..3) → 순서 불일치 0, 우선순위 불일치 0</text>
</svg>
```

정답 실행의 해당 줄 (실제 출력):

```text
== Part B: 커널 순서 == 모델 순서 (무작위 300 회, 동점 많음) ==
  300 회 중 순서 불일치 0, 우선순위 불일치 0
PASS  mism == 0 && prio_mism == 0
  sysconf(_SC_MQ_PRIO_MAX) = 32768, prio 32768 로 send → Invalid argument
```

rbtree 구조는 커널 소스 기억이라 **hedge**: 동작(안정 정렬과 같음)은 위 테스트로 확인했고, 자료구조 이름은 확인하지 않았다. 중요한 점: 우선순위는 **메시지** 의 속성이지 받는 쪽 스레드의 우선순위와는 무관하다. 큐에서 기다리는 여러 수신자 중 누가 먼저 깨는지는 별개 문제다.

### Part B — 가득 참: 막힘 vs EAGAIN

```c
struct mq_attr nb = { .mq_flags = O_NONBLOCK }, old;
if (mq_setattr(q, &nb, &old) != 0) return -errno;
int rc = mq_send(q, buf, len, prio) == 0 ? 0 : -errno;   /* errno 를 먼저 잡는다 */
mq_setattr(q, &old, NULL);                                /* 원래 플래그로 */
return rc;
```

"막힘" 테스트는 타이밍에 기대지 않게 짰다: 큐가 2/2 로 꽉 찬 상태에서 자식이 막히는 `mq_send` 를 하고, 끝나면 파이프에 1 바이트를 쓴다. 부모가 아무것도 받지 않는 한 자식의 send 는 **끝날 수가 없다** → 150 ms 동안 파이프가 조용해야 하고, 부모가 하나 받으면 곧 신호가 온다. 틀린 구현(EAGAIN 으로 즉시 실패)은 첫 단계에서 잡힌다.

```text
== Part B: 가득 참 — O_NONBLOCK 은 EAGAIN, 기본은 막힌다 ==
PASS  rc == -EAGAIN
PASS  q_cur(q) == 2 && (q_flags(q) & O_NONBLOCK) == 0
  자식 send: 150 ms 안에 끝남? 0 → 하나 받은 뒤 끝남? 1 (exit 0)
```

주의: `mq_setattr` 는 **열린 큐 설명(open description)** 의 플래그를 바꾼다. fork 로 물려받은 디스크립터는 같은 설명을 공유하므로 자식이 바꾸면 부모도 바뀐다(연습 1 에서 실측). 여러 프로세스·스레드가 같은 mqd 를 쓰면 "잠깐 O_NONBLOCK" 은 경쟁이다 → 실서비스에서는 처음부터 `O_NONBLOCK` 로 **따로 연** 디스크립터를 쓰는 편이 안전하다(Part C 의 콜백이 그렇게 한다).

### Part B — 타임아웃은 CLOCK_REALTIME 절대 시각

POSIX `mq_timedreceive` 는 `abs_timeout` 을 "Epoch 이후 절대 시각" 으로 받고 기준 시계는 CLOCK_REALTIME 이다. `{0, 3e9 ns}` 같은 **상대 시간을 넘기면 1970-01-01 이 마감** 이라 큐가 비었으면 즉시 ETIMEDOUT 이 된다 — 시작 코드 출력의 "0 ms 걸림" 이 바로 그 모양이다.

| 상황 | 결과 (이 VM, 실측) |
|---|---|
| 빈 큐, 마감 30 ms 뒤 | `-ETIMEDOUT`, 32 ms 걸림 (TSan 빌드 37 ms) |
| 50 ms 뒤 도착, 마감 3000 ms 뒤 | 4 B, prio 3, 59 ms 걸림 |
| 메시지 있음, 마감이 과거(1970) | 메시지를 그냥 받는다 (연습 5) |
| `tv_nsec = -1` (잘못된 값) | 메시지가 있어도 `EINVAL` (연습 5 — 리눅스는 먼저 검사) |

벽시계가 NTP/`date -s` 로 점프하면 마감도 같이 움직인다. 리눅스 mq 에는 `pthread_cond_clockwait` 같은 시계 선택판이 없다 → 짧은 대기는 `poll(mqd, timeout_ms)` (CLOCK_MONOTONIC 기반 상대 시간)로 대신하는 방법이 있다. 이것은 mqd 가 fd 인 리눅스에서만 된다.

### Part B — 속성, EMSGSIZE, 한도

```text
  setattr(flags=O_NONBLOCK, maxmsg=99, msgsize=999) → flags=0x800 maxmsg=4 msgsize=16
PASS  mq_send(q, big, 17, 0) == -1 && errno == EMSGSIZE
PASS  mq_receive(q, small, sizeof small, NULL) == -1 && errno == EMSGSIZE
PASS  q_cur(q) == 1
  msg_max 10  msgsize_max 8192  queues_max 256  msg_default 10  msgsize_default 8192  euid 501
  maxmsg=msg_max+1 → Invalid argument, msgsize=msgsize_max+1 → Invalid argument, maxmsg=0 → Invalid argument
```

수신 버퍼 8 B 에 1 B 메시지를 받으려 해도 `EMSGSIZE` 인 이유: 커널은 "이 버퍼가 **어떤** 메시지라도 담을 수 있나" 를 `cap < mq_msgsize` 로 먼저 판단한다. 메시지는 큐에 그대로 남는다(curmsgs 1). 한도는 세 겹이다 — `demo_limits.c` 실제 출력:

```text
msg_max 10  msgsize_max 8192  queues_max 256  msg_default 10  msgsize_default 8192
RLIMIT_MSGQUEUE cur 819200 max 819200   sysconf(_SC_MQ_PRIO_MAX) 32768   euid 501
attr=NULL 로 연 큐: maxmsg 10 msgsize 8192
기본 크기(10 × 8192 B) 큐: 9 개 만든 뒤 Too many open files (errno 24)
작은(1 × 1 B) 큐: 256 개 만든 뒤 No space left on device (errno 28)
```

| 한도 | 범위 | 넘으면 |
|---|---|---|
| `msg_max`, `msgsize_max` | 큐 하나의 attr 상한 | `EINVAL` (mq_overview(7): msg_max 는 CAP_SYS_RESOURCE 면 무시, HARD_MSGMAX 65536 은 항상) |
| `RLIMIT_MSGQUEUE` (819200 B) | **사용자별** 큐 바이트 합 (메시지 공간 + 커널 부대 비용) | `EMFILE` — 기본 큐 10 × 8192 = 81920 B 라서 9 개째에서 이미 막힘 |
| `queues_max` (256) | 시스템(IPC 네임스페이스) 전체 큐 개수 | `ENOSPC` |

`EMFILE` 은 "fd 가 너무 많다" 로 읽기 쉬워서 디버깅 때 헷갈린다 — mq 에서는 **바이트 한도** 일 수 있다.

### Part C — mq_notify 의 규칙

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">등록 상태 기계 (큐마다 등록자 최대 1 프로세스)</text>
  <rect x="20" y="40" width="160" height="50" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="40" y="70" fill="currentColor">등록 없음</text>
  <rect x="290" y="40" width="160" height="50" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="310" y="70" fill="currentColor">등록됨 (pid P)</text>
  <rect x="560" y="40" width="160" height="50" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="575" y="62" fill="currentColor">알림 발사</text>
  <text x="575" y="80" fill="currentColor" font-size="12">신호 / 새 스레드</text>
  <g stroke="currentColor" fill="none">
    <path d="M180 58 L288 58"/><path d="M450 58 L558 58"/>
    <path d="M640 90 L640 120 L100 120 L100 92"/>
    <path d="M370 90 L370 108 L120 108 L120 92" stroke-dasharray="4 3"/>
  </g>
  <text x="190" y="52" fill="currentColor" font-size="12">mq_notify(sev)</text>
  <text x="458" y="52" fill="currentColor" font-size="12">빈 큐에 도착</text>
  <text x="250" y="136" fill="currentColor" font-size="12">발사 = 등록 자동 해제 (1회용)</text>
  <text x="130" y="104" fill="currentColor" font-size="12">mq_notify(NULL) · P 가 close/exit</text>
  <text x="20" y="168" fill="currentColor">• 다른 프로세스가 mq_notify → EBUSY (실측 -16)</text>
  <text x="20" y="190" fill="currentColor">• 비어 있지 않을 때 도착 → 발사 없음 (등록은 유지)</text>
  <text x="20" y="212" fill="currentColor">• 막힌 mq_receive 가 있으면 메시지는 그쪽으로, 발사 없음 (등록 유지, 연습 4)</text>
  <text x="20" y="240" fill="currentColor" font-weight="bold">콜백 순서: ① 재등록 ② EAGAIN 까지 비우기</text>
  <text x="20" y="264" fill="currentColor">거꾸로 하면: 비움~재등록 사이 도착분은 알림 없음 → 큐가 안 빈 채로 멈춤</text>
</svg>
```

정답 실행 (실제 출력):

```text
== Part C: mq_notify SIGEV_SIGNAL ==
  sig 10  si_code -3 (SI_MESGQ -3)  sival_int 42  si_pid 258560 (child 258560)
  비어있지 않을 때 send → -1, 비운 뒤 send → 10
== Part C: 한 큐에 등록은 한 프로세스만 (EBUSY) ==
  자식 등록 중 부모 → -16 (-EBUSY = -16), 자식 종료(0) 뒤 → 0
== Part C: mqd_t 는 fd — poll 가능 (리눅스) ==
  빈 큐 revents 0x4, 1개 0x5, 가득 0x1 (POLLIN 0x1 POLLOUT 0x4)
== Part C: SIGEV_THREAD — 재등록 후 drain ==
  보냄 9, 콜백이 받음 9, 콜백 호출 5, 재등록 실패 0, 메인 스레드에서 0
51/51 passed
```

- **신호 받기는 sigtimedwait 로**: SIGUSR1/2 를 `main` 맨 앞(스레드가 생기기 전)에서 막아 두고 `sigtimedwait` 로 동기적으로 꺼낸다. 핸들러가 없으니 async-signal-safe 걱정이 없고(OS-B07), 타임아웃으로 "안 왔다" 를 결정적으로 확인한다.
- `si_pid` 는 **보낸 프로세스** (자식 258560) — man mq_notify 에 리눅스 동작으로 적혀 있다(다른 구현은 안 채울 수 있음).
- **EBUSY 해제 시점**: 자식이 `_exit` 하면 디스크립터가 닫히면서 등록이 풀렸다(자식 종료 뒤 부모 등록 = 0).
- **SIGEV_THREAD 의 정체 (glibc)**: man mq_notify NOTES — "raw netlink 소켓을 쓰고, 알림마다 새 스레드를 만든다". 이 VM 의 libc 를 `objdump -d` 해 보면 mq_notify 근처 코드에서 `socket`, `recv`, `pthread_barrier_wait`, `sigfillset`+`pthread_sigmask`(도우미 스레드는 모든 신호를 막는다), 그리고 `bl 8c46c <pthread_create@GLIBC_2.17>` 가 **PLT 를 거치지 않는 직접 호출** 로 보인다. 그래서 clang TSan 빌드는 이 테스트에서 SEGV(exit 139) 로 죽었다 — TSan 이 `pthread_create` 를 가로채지 못해 그 스레드의 상태가 없다(원인은 이 증거로부터의 추론, TSan 내부는 확인 안 함). solution.c 는 TSan 빌드에서만 그 테스트 하나를 건너뛰고 나머지 47/47 을 돌린다.
- 콜백 수 5 < 메시지 9: 한 라운드에 2~3 개를 연달아 보내면 첫 번째가 알림을 만들고, 나머지는 콜백이 비우는 중에 들어와 같은 drain 에 섞인다. 호출 수는 실행마다 5..9 사이에서 달라질 수 있어서 CHECK 은 `calls >= 5 && calls <= sent` 로만 본다.
- **poll**: 빈 큐 = POLLOUT 만, 1 개 = POLLIN|POLLOUT, 가득 = POLLIN 만. epoll 루프(OS-M13, OS-M18)에 mq 를 그대로 넣을 수 있다는 뜻이다. 데몬에서는 SIGEV_THREAD 보다 이쪽이 보통 낫다(스레드 생성 비용·수명 관리 없음).

### 측정 — `demo_bench.c` (mq vs pipe vs Unix 소켓)

OS-B14 하네스의 축소판: 같은 `send_m/recv_m` 으로 pipe(2개), `socketpair(SOCK_STREAM)`, `socketpair(SOCK_SEQPACKET)`, mq(2개, maxmsg 10, msgsize 8192)를 돈다. 부모가 n 바이트 보내고 자식이 돌려줄 때까지 = 왕복 1회, 20000 회 × 5 run, warm-up 1000.

```bash
gcc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o bench
./bench rtt ; ./bench bulk ; ./bench pair          # 프로세스가 아무 CPU 에나
taskset -c 3 ./bench rtt ; taskset -c 3 ./bench bulk 3   # 부모·자식을 vCPU 하나에
```

실측 (VM load average 0.25 → 1.75, **호스트 맥 load 19.5** — 다른 세션들이 동시에 돌았다. 비율만 믿을 것). RTT ns, min / median / p99:

| size | pipe | stream | seqpkt | mq |
|---|---|---|---|---|
| 1 | 1500 / 25001 / 107418 | 2791 / 22458 / 104042 | 1917 / 29500 / 187793 | 6583 / 30417 / 140668 |
| 64 | 1333 / 24334 / 91460 | 2875 / 22875 / 141126 | 1791 / 31126 / 192377 | 1750 / 30000 / 149835 |
| 1024 | 1541 / 24958 / 75042 | 3083 / 23583 / 104709 | 2792 / 29334 / 105459 | 1583 / 30750 / 130459 |
| 8192 | 5334 / 26709 / 79001 | 3667 / 25708 / 133793 | 2916 / 33292 / 121084 | 11834 / 34708 / 136751 |

같은 것을 `taskset -c 3` (부모·자식 같은 vCPU):

| size | pipe | stream | seqpkt | mq |
|---|---|---|---|---|
| 1 | 1333 / 1833 / 2708 | 1792 / 2875 / 4542 | 1708 / 2375 / 3375 | 1458 / 1958 / 2625 |
| 64 | 1375 / 1833 / 2625 | 1833 / 3084 / 5167 | 1667 / 2375 / 3792 | 1458 / 2000 / 2708 |
| 1024 | 1416 / 1917 / 2750 | 1875 / 2958 / 5125 | 1750 / 2500 / 3750 | 1541 / 2001 / 3000 |
| 8192 | 1875 / 2625 / 4125 | 2625 / 3959 / 7500 | 2666 / 3875 / 6500 | 2458 / 3708 / 5750 |

단방향 처리량 (msgs/s, 중앙값) 과 블록 없는 send+recv 한 쌍 (프로세스 1개, 64 B):

| | pipe | stream | seqpkt | mq |
|---|---|---|---|---|
| 64 B, CPU 자유 | 2 467 822 | 1 378 239 | 987 438 | **338 759** |
| 64 B, 같은 vCPU | 2 165 454 | 845 693 | 850 597 | 741 308 |
| 8192 B, CPU 자유 | 428 204 (3508 MB/s) | 445 826 | 356 760 | 384 815 (3152 MB/s) |
| send+recv 한 쌍 | 712 ns | 853 ns | 718 ns | 558 ns |

읽는 법:

1. **syscall 자체는 mq 가 가장 싸다** (한 쌍 558 ns). 같은 vCPU 에서 RTT 도 pipe 와 거의 같다(64 B 2.0 µs vs 1.8 µs). "mq 는 느리다" 는 맞지 않다.
2. CPU 가 자유로우면 median 이 모두 **22–35 µs** 로 뛴다. 이것은 IPC 가 아니라 "다른 vCPU 에서 자고 있던 상대를 깨우는" 비용(VM 의 vCPU idle → 호스트 스레드 깨우기 + IPI)이다. 네 방식 공통 — 지연 요구가 빡빡하면 IPC 종류보다 **CPU 배치(affinity)와 idle 정책** 이 먼저다.
3. 64 B 처리량에서 mq 가 pipe 의 1/7 인 이유는 **큐 깊이 10**: 생산자는 10 개마다 막혀 문맥 교환을 기다린다. pipe 는 64 KiB = 64 B 메시지 1024 개를 쌓는다. 같은 vCPU 에 묶으면 차이가 1/3 로 준다. 비특권 데몬에서 mq 를 고속 스트림으로 쓰지 않는 이유다 — 고속 대량 데이터는 shm 링(OS-B14, OS-A12), mq 는 **명령·경보·작은 이벤트**.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

검증 (VM, 실제 결과): gcc·clang `-Wall -Wextra -Werror -O2 -pthread` 경고 0, `51/51 passed` 5 회 연속 + `cd /` 에서 실행도 51/51 (0.52 s). gcc·clang `-fsanitize=address,undefined` 둘 다 51/51 깨끗. clang `-fsanitize=thread` 는 SIGEV_THREAD 테스트를 건너뛴 47/47, 경고 0. 실행 뒤 `ls /dev/mqueue` 비어 있음.

## 7. 자주 하는 실수

**1. 상대 시간을 mq_timedreceive 에 넘김** — 마감이 1970 년이라 큐가 비었으면 즉시 ETIMEDOUT. 폴링 루프가 CPU 를 태우거나, 반대로 "타임아웃이 안 걸린다" 로 오해한다.

```c
struct timespec rel = { 0, 50 * 1000000L };
mq_timedreceive(q, buf, cap, NULL, &rel);           /* 틀림: 1970-01-01 00:00:00.05 */
abs_deadline_ms(50, &dl);                           /* 맞음: CLOCK_REALTIME now + 50 ms */
mq_timedreceive(q, buf, cap, NULL, &dl);
```

**2. 수신 버퍼를 "예상 메시지 크기" 로 잡음** — `cap < mq_msgsize` 면 메시지가 1 B 여도 EMSGSIZE. `mq_getattr` 로 `mq_msgsize` 를 읽어 버퍼를 잡는다.

```c
struct mq_attr a; mq_getattr(q, &a);
char *buf = malloc((size_t)a.mq_msgsize);
```

**3. 콜백에서 비운 뒤 재등록** — 그 사이에 도착한 메시지는 알림을 못 만들고, 재등록할 땐 큐가 비어 있지 않아 다음 알림도 안 온다 → 소비자가 영영 안 깬다. 순서는 **재등록 → 비우기**, 비우기는 O_NONBLOCK 디스크립터로 EAGAIN 까지.

**4. attr 를 NULL 로 열고 깊이를 믿음** — 기본은 `msg_default`(10) × `msgsize_default`(8192). 게다가 비특권이면 `maxmsg` 를 11 로 달라고 해도 EINVAL. 설치 스크립트에서 `sysctl fs.mqueue.msg_max=…` 를 올리거나 깊이 10 에 맞게 설계한다. 9 개째 큐에서 EMFILE 이 나면 fd 가 아니라 RLIMIT_MSGQUEUE 를 의심한다.

**5. mq_unlink 를 안 함** — 이름은 프로세스보다 오래 산다. 크래시 후 재시작하면 `O_CREAT|O_EXCL` 이 EEXIST, 혹은 `O_CREAT` 만 쓰면 **예전 메시지가 남은 큐** 를 다시 연다. 시작 때 `mq_unlink(name)` 후 생성하거나, 생성 직후 unlink 하고 fd 로만 공유(fork 계층일 때 — demo_bench 가 이렇게 한다).

**6. 공유 디스크립터에 mq_setattr** — fork 로 물려받은 mqd 에 O_NONBLOCK 을 켜면 다른 프로세스의 blocking receive 도 갑자기 EAGAIN 을 돌려준다 (연습 1). 용도별로 `mq_open` 을 따로 한다.

**7. 시그널 알림을 sigtimedwait 로 받으면서 마스크를 늦게 설정** — 다른 스레드(예: SIGEV_THREAD 를 쓰면 생기는 스레드들)가 신호를 막지 않았으면 그쪽에서 기본 동작(SIGUSR1 = 종료)이 일어날 수 있다. 스레드를 만들기 **전에** `pthread_sigmask(SIG_BLOCK)` 한다 (glibc 의 mq 도우미 스레드 자체는 모든 신호를 막는다 — objdump 에서 sigfillset+pthread_sigmask 확인).

## 8. 면접 Q&A

<details>
<summary>Q1. pipe·Unix 소켓·POSIX mq 를 어떻게 고르나요?</summary>

바이트 스트림 대량 전송은 pipe/STREAM 소켓(가장 높은 처리량), fd 전달·피어 인증·양방향·네트워크로 확장은 Unix 소켓(OS-M16), **작은 메시지 + 우선순위 + 메시지 경계 + 이름으로 아무 프로세스나 접속** 이 필요하면 mq. 이 VM 실측에서 mq 는 syscall 한 쌍이 가장 쌌고(558 ns), 같은 CPU RTT 는 pipe 와 비슷했지만, 비특권 깊이 10 때문에 64 B 스트림 처리량이 pipe 의 1/7 이었다. 대용량은 shm(OS-B14).

</details>

<details>
<summary>Q2. 같은 우선순위 메시지의 순서는? 생산자가 둘이면?</summary>

같은 우선순위끼리는 큐에 들어간 순서(FIFO). 생산자가 둘이면 **생산자 사이** 순서는 스케줄링에 달려 정해지지 않지만, **한 생산자 안** 의 순서는 지켜진다 — Part A 가 생산자별 seq 로 검사하는 것이 이것이다. 전체 순서가 필요하면 seq 를 한 곳에서 매기거나 소비자에서 재정렬한다.

</details>

<details>
<summary>Q3. 왜 mq_timedreceive 는 절대 시각을 받나요? 단점은?</summary>

재시도(EINTR, 가짜 깨움) 때 마감을 다시 계산할 필요가 없어서다 — 같은 `ts` 로 다시 부르면 남은 시간만 기다린다(pthread_cond_timedwait 와 같은 설계). 단점은 CLOCK_REALTIME 기준이라 벽시계가 점프하면 대기도 영향을 받는다는 것. 리눅스에서는 mqd 가 fd 라서 `poll(fd, POLLIN, ms)` 로 단조 시계 기반 대기를 할 수 있다(비이식).

</details>

<details>
<summary>Q4. mq_notify 는 언제 알림을 보내고, 왜 재등록을 먼저 하나요?</summary>

큐가 **비어 있다가** 메시지가 들어올 때만, 그리고 그 순간 막혀 있는 `mq_receive` 가 없을 때만. 한 번 보내면 등록이 풀린다. 비운 뒤 재등록하면 "비움 ~ 재등록" 사이 도착분이 알림을 못 만들고, 재등록 시점엔 큐가 비어 있지 않아 다음 알림도 없다 → 멈춤. 먼저 재등록하면 그 뒤 들어오는 것은 (a) 지금 비우는 루프가 가져가거나 (b) 큐가 빈 다음 도착이면 새 알림이 된다. man mq_notify 도 "before emptying" 이라고 적는다.

</details>

<details>
<summary>Q5. 다른 프로세스가 이미 mq_notify 등록을 했으면? 그 프로세스가 죽으면?</summary>

`EBUSY`. 큐마다 등록자는 하나(프로세스 단위). 등록자가 `mq_notify(q, NULL)` 로 해제하거나, 그 큐 디스크립터를 닫거나 종료하면 해제된다 — 이 레슨에서 자식이 `_exit` 한 뒤 부모 등록이 0 으로 성공했다. `/dev/mqueue/<name>` 을 읽으면 `NOTIFY_PID` 로 누가 등록했는지 보인다(연습 2).

</details>

<details>
<summary>Q6. SIGEV_THREAD 는 내부적으로 어떻게 동작하나요? 비용은?</summary>

리눅스 커널은 신호 또는 netlink 메시지만 보낸다. glibc 는 처음 SIGEV_THREAD 등록 때 netlink 소켓과 도우미 스레드(모든 신호 차단)를 만들고, 알림이 올 때마다 **새 스레드** 를 만들어 콜백을 부른다(man mq_notify NOTES; libc objdump 에서 socket/recv/pthread_create 호출 확인). 그래서 알림마다 스레드 생성 비용이 있고, 콜백이 동시에 여럿 돌 수 있으며(카운터는 원자적으로), TSan 같은 도구가 그 스레드를 못 볼 수 있다. 데몬이라면 mqd 를 epoll 에 넣는 편이 단순하다.

</details>

<details>
<summary>Q7. /proc/sys/fs/mqueue 한도와 RLIMIT_MSGQUEUE 는 무엇을 막고 errno 는?</summary>

`msg_max`(10)·`msgsize_max`(8192) 는 큐 하나의 attr 상한 → 넘으면 `EINVAL`(CAP_SYS_RESOURCE 는 msg_max 무시, HARD_MSGMAX 는 항상). `queues_max`(256) 는 시스템 전체 큐 개수 → `ENOSPC`. `RLIMIT_MSGQUEUE`(819200 B) 는 **사용자별** 바이트 합 → `EMFILE`. 이 VM 에서 기본 크기 큐는 9 개째에 EMFILE, 1×1 B 큐는 256 개째에 ENOSPC 가 났다.

</details>

<details>
<summary>Q8. RTOS 큐(FreeRTOS)와 POSIX mq 를 비교하면?</summary>

둘 다 **복사 의미론** (send 때 사본, RTOS-B11) 이고 블로킹·타임아웃·고정 항목 크기를 가진다. 차이: FreeRTOS 큐는 메시지 우선순위가 없고 앞에 넣기(`xQueueSendToFront`)만 있다, 대기 태스크는 태스크 우선순위 순으로 깬다. ISR 용 `FromISR` (절대 안 막힘)가 있다. mq 는 메시지마다 우선순위, 이름으로 프로세스 간 공유, 커널 경계를 넘는 두 번의 복사(사용자→커널→사용자)와 syscall 비용이 있고, 타임아웃이 틱이 아니라 절대 벽시계 시각이다. RTOS 의 "ISR → 태스크" 자리는 리눅스에선 드라이버 → 사용자 공간(eventfd/poll, OS-M18)이다.

</details>

## 9. 추가 연습

practice.c 하나에 다섯 답을 모두 넣었다. 각 연습은 자기 큐를 만들고 끝에 이름을 지운다.

### 연습 1 — fork 와 열린 큐 설명

부모가 연 mqd 를 자식이 물려받아 `mq_setattr(O_NONBLOCK)` 하면 부모의 `mq_getattr` 는? 같은 이름을 부모가 `mq_open` 으로 한 번 더 연 디스크립터는?

<details>
<summary>답</summary>

부모 q 의 flags = `0x800`(O_NONBLOCK), 따로 연 other = `0`. 부모의 blocking receive 가 이제 EAGAIN. fork 는 fd 와 같은 open description 을 공유하고, `mq_open` 은 새 description 을 만든다. 실제 출력: `자식이 O_NONBLOCK 켠 뒤: 부모 q flags 0x800, 따로 연 other flags 0`.

</details>

### 연습 2 — /dev/mqueue 들여다보기

10 B, 20 B 메시지를 넣고 `/dev/mqueue/<name>` 을 읽어 파싱하라. SIGEV_SIGNAL(SIGUSR1) 등록 전후로 무엇이 바뀌나?

<details>
<summary>답</summary>

`QSIZE:30 NOTIFY:0 SIGNO:0 NOTIFY_PID:0` → 등록 후 `SIGNO:10 NOTIFY_PID:<내 pid>`. QSIZE 는 사용자 데이터 바이트 합(mq_overview(7): 3.5–4.1 에서는 커널 부대 비용이 섞이던 회귀가 있었다). NOTIFY 는 sigev_notify 값(SIGEV_SIGNAL = 0)이라 등록 전에도 0 → NOTIFY_PID 로 등록 여부를 판단한다.

</details>

### 연습 3 — RLIMIT_MSGQUEUE

자식에서 `RLIMIT_MSGQUEUE` 를 16384 B 로 낮추고 4 × 1024 B 큐를 계속 만들면 몇 개에서 무슨 errno?

<details>
<summary>답</summary>

3 개 뒤 `EMFILE`. 4 × 4096 = 16384 이지만 큐마다 커널 부대 비용(메시지 헤더·트리 노드)이 더해져 4 개째는 넘는다. 바이트는 **사용자별** 합이라 같은 uid 의 다른 큐도 함께 센다 → 테스트는 `≤ 3` 과 `EMFILE` 만 확인한다.

</details>

### 연습 4 — 막힌 수신자 vs 알림

부모가 SIGEV_SIGNAL 등록, 자식은 빈 큐에서 `mq_receive` 로 막혀 있다. 부모가 보내면 신호가 오나? 자식이 막혔는지는 어떻게 확실히 아나?

<details>
<summary>답</summary>

신호는 안 온다 — 메시지가 바로 자식에게 간다(man mq_notify). 등록은 **유지** 되어 그 뒤 빈 큐에 보내면 신호가 온다. 자식이 커널 안에서 자는지는 `/proc/<pid>/syscall` 첫 숫자가 `SYS_mq_timedreceive`(aarch64 = 183)인지로 확인한다(glibc 의 `mq_receive` 는 timeout NULL 인 mq_timedreceive syscall). 실제 출력: `자식이 mq_timedreceive(183) 에서 잠듦 1, 자식 exit 0, 부모 알림 -1, 큐 0 개`.

</details>

### 연습 5 — 잘못된 / 지난 마감

메시지가 있는 큐에 `tv_nsec = -1` 마감으로 받으면? 마감이 1970 년이면? 빈 큐 + 1970 년이면?

<details>
<summary>답</summary>

리눅스: 메시지가 있어도 `EINVAL`(POSIX 는 "꺼낼 수 있으면 검사하지 않아도 된다" 고 허용만 한다 — 리눅스는 syscall 입구에서 먼저 검사). 지난 마감 + 메시지 = 그냥 받음. 빈 큐 + 지난 마감 = `ETIMEDOUT`. 실제: `메시지 있음+잘못된 ts → -1 (Invalid argument), 메시지 있음+지난 시각 → 1, 빈 큐+지난 시각 → -1 (Connection timed out)`.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
# VM 안, /tmp/OS-M15
gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr      # 13/13 passed, 0.2 s
gcc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread practice.c -o pr_asan && ./pr_asan
```

`13/13 passed` (gcc·clang, ASan/UBSan 포함, 다른 cwd 에서도 같음).

## 10. 핵심 정리

- mq = 커널이 **메시지 경계 + 우선순위(높은 것 먼저, 같으면 FIFO)** 를 지키는 IPC. 리눅스에서 mqd_t 는 fd → poll/epoll 가능. macOS 에는 없다.
- 수신 버퍼는 `mq_msgsize` 이상(아니면 1 B 도 EMSGSIZE). 가득 참: 기본 = 막힘, O_NONBLOCK = EAGAIN, timed = ETIMEDOUT.
- `mq_timed*` 의 시간은 **CLOCK_REALTIME 절대 시각**. EINTR 재시도는 같은 마감으로.
- `mq_setattr` 는 O_NONBLOCK 만 바꾸고, 그 플래그는 fork 로 공유된 open description 전체에 걸린다.
- `mq_notify`: 1회용 · 빈→비어있지않음 때만 · 막힌 수신자가 있으면 무시 · 큐당 1 프로세스(EBUSY). 콜백은 **재등록 먼저, O_NONBLOCK 으로 비우기 나중**.
- 한도: msg_max 10 / msgsize_max 8192 → EINVAL, RLIMIT_MSGQUEUE 819200 B (사용자별) → EMFILE, queues_max 256 → ENOSPC.
- 이름은 파일처럼 오래 산다 → pid 넣은 이름 + `mq_unlink`. glibc 2.34+ 는 `-lrt` 불필요.
- 성능(VM): syscall 한 쌍은 mq 가 가장 쌌고 같은 CPU RTT ≈ pipe(≈2 µs), 하지만 깊이 10 때문에 작은 메시지 스트림 처리량은 pipe 의 1/7 → mq 는 명령·경보용.

## 참고자료

- POSIX.1-2017: [mq_open](https://pubs.opengroup.org/onlinepubs/9699919799/functions/mq_open.html), [mq_send](https://pubs.opengroup.org/onlinepubs/9699919799/functions/mq_send.html), [mq_receive / mq_timedreceive](https://pubs.opengroup.org/onlinepubs/9699919799/functions/mq_receive.html), [mq_notify](https://pubs.opengroup.org/onlinepubs/9699919799/functions/mq_notify.html), [mq_setattr](https://pubs.opengroup.org/onlinepubs/9699919799/functions/mq_setattr.html), [<mqueue.h>](https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/mqueue.h.html)
- Linux man-pages: [mq_overview(7)](https://man7.org/linux/man-pages/man7/mq_overview.7.html) (한도, /dev/mqueue, poll, QSIZE BUGS — VM 의 로컬 man 으로 확인), [mq_notify(3)](https://man7.org/linux/man-pages/man3/mq_notify.3.html) (si_pid, "before emptying", glibc netlink NOTES), [mq_open(3)](https://man7.org/linux/man-pages/man3/mq_open.3.html), [mq_getattr(3)](https://man7.org/linux/man-pages/man3/mq_getattr.3.html), [getrlimit(2)](https://man7.org/linux/man-pages/man2/getrlimit.2.html) (RLIMIT_MSGQUEUE)
- 리눅스 소스 `ipc/mqueue.c` (우선순위 rbtree, 파이프라인 전달 — 이 레슨에서는 동작만 테스트로 확인)
- OSTEP: 이 주제는 본문 장이 따로 없다 — IPC 일반은 ch.5 (Process API) 와 동시성 파트(ch.26–31)의 생산자/소비자(ch.30 조건 변수) 배경으로 본다
- 관련 레슨: OS-B14 (IPC 비교 하네스), OS-B07 (시그널·sigtimedwait 배경), OS-M13 / OS-M18 (epoll 루프에 fd 통합), OS-M16 (Unix 소켓 심화), OS-M17 (프로세스 공유 뮤텍스), OS-A12 (견고한 shm 프로토콜), RTOS-B11 (복사 큐), RTOS-M06 (블로킹 큐 구현)
