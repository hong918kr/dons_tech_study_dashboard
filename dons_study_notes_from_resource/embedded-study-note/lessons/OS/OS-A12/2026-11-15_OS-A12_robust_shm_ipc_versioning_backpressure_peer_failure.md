---
id: OS-A12
time: 120분
updated: 2026-11-15
---
# OS-A12 견고한 공유 메모리 IPC: 버전·백프레셔·상대 프로세스 장애 감지

## 학습 목표

- shm 링(DS-A01 축약판) 앞에 **ABI 고정 접두사(magic·version·hdr_size) + 레이아웃 필드** 를 두고, magic 을 **맨 마지막에 release 로** 써서 "다 만들어졌다" 를 알리며, 버전·레이아웃이 다른 작성자(v2)와 만들다 죽은 흔적을 거부한다.
- 슬롯마다 **seqlock 마커(2p+1 쓰는 중 / 2p+2 게시됨) + 체크섬** 을 두어 소비자가 찢어진(torn) 메시지를 절대 받지 않게 하고, 메시지 seq 번호의 빈틈으로 **유실을 정확히 센다**.
- 가득 참 정책 **BLOCK(제한 시간 + 백오프) / DROP_NEWEST / OVERWRITE_OLDEST** 를 구현하고, OVERWRITE 에서 늦은 소비자가 덮어쓰인 슬롯을 감지해 최신 창으로 재동기화하는 셈법을 스레드 스트레스의 **정확한 불변식** 으로 검증한다.
- 상대 생존을 **하트비트 카운터 정체 + `kill(pid,0)`==ESRCH** 로 판단하고, 좀비·pid 재사용·EPERM 이 왜 `kill` 만으로는 안 되는지 실측으로 보인다. 재시작한 생산자는 **세대(generation) +1, roll-forward 복구, 세대 울타리(fencing)** 로 이어 쓴다.
- fork 한 자식이 정해진 지점에서 `raise(SIGKILL)` 하는 **크래시 주입** 으로 "찢어진 메시지 0, 사망 감지, 재시작 후 정확히 한 번" 을 매 실행 검증하고, 소비자 사망은 **at-least-once** 가 되는 이유와 커밋 순서를 설명한다.

## 1. 문제

"공유 메모리 IPC 가 빠른 건 알겠는데, 상대 프로세스가 죽으면 어떻게 아나요? 다시 살아나면?" — 카메라·IMU 데몬 → 추론/제어 프로세스, 듀얼코어 MCU 의 코어 간 메일박스처럼 **복사 없는 채널** 이 필요한 곳에서 시니어·스태프 면접이 반드시 파고드는 질문이다. OS-B14 는 shm 링이 큰 데이터에서 빠르다는 것과 함께 "상대가 죽으면 읽는 쪽은 **영원히** 기다린다" 를 보였다. 파이프·소켓은 커널이 EOF/EPIPE 를 알려 주지만, 공유 메모리는 그냥 메모리라 **아무도 알려 주지 않는다**. 리눅스라면 robust mutex(OS-M17)로 "락 주인의 죽음" 은 알 수 있지만 macOS 에는 그 API 가 없고, 락 없는 SPSC 링(DS-A01)에는 애초에 락이 없다. 그래서 이 레슨은 **프로토콜 자체** 에 버전·하트비트·세대·체크섬을 넣어 장애를 감지하고 복구한다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">shm 객체 /OS-A12.&lt;pid&gt;.&lt;n&gt; : 헤더 3줄(줄마다 쓰는 쪽 하나) + 슬롯 cap 개 × 256 B</text>
  <rect x="10" y="35" width="230" height="110" class="f-mut" stroke="currentColor"/>
  <text x="18" y="54" fill="currentColor" font-weight="bold">줄 0 — 만들 때 1회 (불변)</text>
  <text x="18" y="74" fill="currentColor" font-family="monospace" font-size="12">magic  ← 맨 마지막 release</text>
  <text x="18" y="92" fill="currentColor" font-family="monospace" font-size="12">version, hdr_size (+0,+4,+6 고정)</text>
  <text x="18" y="110" fill="currentColor" font-family="monospace" font-size="12">slot_size, capacity, total</text>
  <text x="18" y="128" fill="currentColor" font-family="monospace" font-size="12">policy, features</text>
  <rect x="255" y="35" width="230" height="110" class="f-ok" stroke="currentColor"/>
  <text x="263" y="54" fill="currentColor" font-weight="bold">줄 1 — 생산자만 씀</text>
  <text x="263" y="74" fill="currentColor" font-family="monospace" font-size="12">tail (게시 위치 힌트)</text>
  <text x="263" y="92" fill="currentColor" font-family="monospace" font-size="12">prod_hb   하트비트 카운터</text>
  <text x="263" y="110" fill="currentColor" font-family="monospace" font-size="12">prod_pid, prod_gen</text>
  <text x="263" y="128" fill="currentColor" font-family="monospace" font-size="12">dropped</text>
  <rect x="500" y="35" width="230" height="110" class="f-acc" stroke="currentColor"/>
  <text x="508" y="54" fill="currentColor" font-weight="bold">줄 2 — 소비자만 씀</text>
  <text x="508" y="74" fill="currentColor" font-family="monospace" font-size="12">head (커밋된 읽기 위치)</text>
  <text x="508" y="92" fill="currentColor" font-family="monospace" font-size="12">cons_hb   하트비트 카운터</text>
  <text x="508" y="110" fill="currentColor" font-family="monospace" font-size="12">cons_pid, cons_gen</text>
  <rect x="10" y="165" width="720" height="44" fill="none" stroke="currentColor"/>
  <rect x="10" y="165" width="110" height="44" class="f-warn" stroke="currentColor"/>
  <text x="16" y="183" fill="currentColor" font-family="monospace" font-size="12">w0 마커</text>
  <text x="16" y="200" fill="currentColor" font-family="monospace" font-size="12">2p+1 / 2p+2</text>
  <rect x="120" y="165" width="110" height="44" class="f-mut" stroke="currentColor"/>
  <text x="126" y="183" fill="currentColor" font-family="monospace" font-size="12">w1 gen|len</text>
  <rect x="230" y="165" width="100" height="44" class="f-mut" stroke="currentColor"/>
  <text x="236" y="183" fill="currentColor" font-family="monospace" font-size="12">w2 msg_seq</text>
  <rect x="330" y="165" width="100" height="44" class="f-mut" stroke="currentColor"/>
  <text x="336" y="183" fill="currentColor" font-family="monospace" font-size="12">w3 FNV-1a</text>
  <text x="440" y="183" fill="currentColor" font-family="monospace" font-size="12">w4..w31 payload (최대 224 B)</text>
  <text x="440" y="200" fill="currentColor" font-size="12">모든 워드 = _Atomic uint64_t</text>
  <text x="10" y="232" fill="currentColor">슬롯 = 마커가 "게시됨(2p+2)" 일 때만 존재. tail 은 캐시일 뿐 — 재시작 때 진실은 마커에서 다시 읽는다.</text>
  <text x="10" y="252" fill="currentColor">메시지는 (gen, msg_seq) 를 달고 다닌다 → gen 이 바뀌면 재동기화, 같은 gen 에서 seq 가 뛰면 유실.</text>
  <text x="10" y="272" fill="currentColor">포인터는 없다: 두 프로세스의 매핑 주소가 다르므로 위치·오프셋만 저장 (OS-M17 그림과 같은 이유).</text>
  <text x="10" y="292" fill="currentColor">C11 7.17.5 NOTE: lock-free 원자 연산은 address-free — 다른 주소로 매핑돼도 원자적.</text>
</svg>
```

**환경 (이 레슨의 모든 실측)**: Apple M6 (2 Super + 4 Performance + 6 Efficiency, `hw.pagesize` 16384, 캐시 라인 128), macOS 27.2, `Apple clang version 21.0.0`, SDK 26.4. 기계는 다른 작성자들과 공유 중이라 부하가 높다 (측정 때 `uptime` load average 16~19) — 시간 값은 비율로만 읽는다.

**이 Mac 에서 먼저 확인한 사실** (`/tmp/OS-A12_work/probe.c`, 레슨 폴더에는 넣지 않음 — 결과만 옮긴다):

```text
shm_open fd=3
ftruncate(1000)=0
fstat size=16384                                  ← 크기가 페이지 단위로 올림된다
ftruncate again same=-1 errno=Invalid argument    ← 두 번째 ftruncate 는 같은 크기여도 EINVAL
waitid WNOWAIT=0 si_pid==c 1 si_status=7          ← 회수하지 않고 "끝났음" 만 확인
kill(zombie,0)=0 errno=-                          ← 좀비는 kill 에 "있다"
proc_pidinfo zombie n=0 status=0                  ← 신원 조회는 좀비에서 실패
after reap kill=-1 errno=No such process
EVFILT_PROC on non-child register=0               ← 내 자식이 아닌 프로세스(손자)도 NOTE_EXIT 감시 가능
kevent got=1 ident==g 1 fflags NOTE_EXIT 1
wait ret=0 errno=- val=1 after 101.1 ms           ← os_sync_wait_on_address(SHARED) 를 다른 프로세스가 깨움
32-byte name: -1 File name too long               ← PSHMNAMLEN 31 (sys/posix_shm.h:53)
```

### Part A — 헤더·슬롯 프로토콜 (Version, seqlock, 체크섬, 유실)

- **TODO(A1)** `rb_validate_header(h, mapped)` — 매핑 < 헤더 또는 magic(acquire) 불일치 → `RB_ENOTREADY`; version ≠ 1 → `RB_EVERSION`; hdr_size·slot_size 불일치, capacity 가 0/2의 거듭제곱 아님/4096 초과, `total_size ≠ hdr + cap×slot`, `total_size > mapped`, policy > 2 → `RB_ELAYOUT`.
- **TODO(A2)** `slot_publish(r, pos, msg_seq, data, len)` — 마커 2p+1 → release fence → payload(절반마다 크래시 지점) → meta·msg_seq → 체크섬 → 마커 2p+2 (release).
- **TODO(A3)** `slot_read(r, pos, v)` — seqlock 읽기 (acquire 마커 → relaxed 복사 → acquire fence → 마커 재확인) + 길이·체크섬 검사 → `SLOT_OK / NOTREADY / OVERRUN / CORRUPT`.
- 주어진 것: `rb_create` (magic 마지막, 재사용 시 ftruncate 생략), `rb_attach` (fstat → mmap → A1), `rb_close`, FNV-1a, `CRASH_POINT` 매크로.
- 테스트: 메모리 위 헤더 11 가지 변형, shm 위 v2 작성자·슬롯 512 B 작성자·"16 KB 객체에 1 MB 링" 주장·크기 0 객체 거부, 서로 다른 두 매핑 간 송수신, 홀수 마커 슬롯은 안 보임, 게시된 슬롯 1 비트 뒤집기 → `RB_CORRUPT` (같은 위치는 한 번만 셈) → 복원하면 받음, DROP_NEWEST 로 버린 2 개 = 소비자가 센 gap 2 개.

### Part B — 가득 참 정책과 수신 (backpressure)

- **TODO(B1)** `rb_send` — 세대 울타리(`prod_gen ≠ 내 gen` → `RB_FENCED`), 정책별 처리, 게시 후 tail(release).
- **TODO(B2)** `rb_recv` — 슬롯 상태별 처리: OK 면 (gen, msg_seq) 추적(resync / lost / dups), OVERRUN 이면 `max(tail − cap + 1, pos + 1)` 로 점프, 비었으면 상대 생존을 보면서 백오프 대기, 상대가 죽었으면 **한 번 더 본 뒤** `RB_PEER_DEAD`.
- 테스트: BLOCK 4칸 — 5 번째는 `RB_FULL`(대기 없음)·`RB_TIMEOUT`(20 ms, 상대 생존), 한 칸 비면 OK. 새 생산자 핸들이 시작하면 옛 핸들은 `RB_FENCED`. OVERWRITE 4칸 — 0 을 받고 1..10 을 보낸 뒤 받으면 8, 9, 10 (lost 7, overruns 1). 스레드 스트레스 3 정책 × 20 만 메시지 (아래 불변식).

### Part C — 생존 감지·재시작·크래시 주입

- **TODO(C1)** `rb_peer_state(r, now)` — `NONE / ALIVE / STALE / GONE` (시계는 인자로 받는다 → 가짜 시계로 결정적 테스트).
- **TODO(C2)** `rb_start(r)` — 생산자: gen+1, roll-forward 로 tail 복구, 이어 보낼 msg_seq 힌트. 소비자: gen+1, head 에서 재개.
- 테스트: 가짜 시계로 "정확히 stale 만큼 정체하면 STALE", 박동이 오면 회복, 새 세대는 새 감시, **좀비는 kill 로는 살아 있고 하트비트로 STALE**, 회수 후 GONE. 복구: "게시됐지만 tail 못 올림" → rolled 1, "쓰다 만 칸" → 다시 씀.
- 크래시 주입 (fork + `raise(SIGKILL)`): 헤더 쓰다 죽음, 생산자 7 지점 (마커만, payload 절반, seq 와 체크섬 사이, 게시 직전, 게시 후 tail 전, 다 보낸 뒤, **일부러 틀린 조기 게시**), 소비자 2 지점 (처리 후 커밋 전/후). 감독자(부모)가 죽은 자식을 회수하고 재시작한다.

## 2. Problem (English)

> Build a single-producer/single-consumer message channel in a POSIX shared-memory object that survives the
> death of either side. Two unrelated processes map the object at different addresses.
>
> ```c
> int rb_validate_header(const rb_hdr_t *h, size_t mapped);   // OK / ENOTREADY / EVERSION / ELAYOUT
> void slot_publish(rb_t *r, uint64_t pos, uint64_t msg_seq, const void *data, uint32_t len);
> int  slot_read(rb_t *r, uint64_t pos, rb_view_t *v);         // OK / NOTREADY / OVERRUN / CORRUPT
> int  rb_send(rb_t *r, const void *data, uint32_t len, uint64_t msg_seq, int timeout_ms);
> int  rb_recv(rb_t *r, void *buf, size_t cap, rb_view_t *info, int timeout_ms);
> int  rb_peer_state(rb_t *r, uint64_t now_ns);                // NONE / ALIVE / STALE / GONE
> int  rb_start(rb_t *r);                                      // take the role: new generation, recover
> ```
>
> Requirements: (1) a reader must refuse a header it does not understand (magic, version, layout) and must
> never read a half-initialized one; (2) a consumer must never accept a torn message — per-slot sequence
> marker plus checksum; (3) full-ring policy is BLOCK (bounded wait), DROP_NEWEST, or OVERWRITE_OLDEST, and
> the consumer counts lost messages exactly from message sequence gaps; (4) each side detects the other's
> death (heartbeat staleness, `kill(pid,0)` == ESRCH) instead of waiting forever; (5) a restarted producer
> takes a new generation, recovers its position from the slot markers, and the consumer resynchronizes.
> Verify with fault injection: kill the producer at every interesting point of a write and the consumer at
> both sides of its commit; every message 0..N-1 must be processed exactly once (producer death) or at least
> once with exactly one duplicate (consumer killed after processing, before commit).
> Follow-ups: why a counter and not a timestamp for the heartbeat? What if the "dead" producer was only
> stopped? How would you extend this to one producer and many consumers?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

특별한 빌드 플래그는 없다 (macOS 는 pthread·shm 이 libSystem 에 있어 `-pthread`·`-lrt` 불필요). 처음 실행 결과 (실제 출력 일부, 0.13 초, exit 1 — 멈추지 않는다):

```text
OS-A12 robust shm IPC (pid 95588)
Part A — header / slot seqlock / checksum / gap
FAIL starter.c:683  rb_validate_header(h, big) == RB_OK
...
FAIL starter.c:749  c.lost == atomic_load(&p.h->dropped) && c.received == 9u && c.dups == 0u
Part B — BLOCK / DROP_NEWEST / OVERWRITE_OLDEST
FAIL starter.c:761  rb_create(&p, nm, 4u, RB_BLOCK) == RB_OK && rb_attach(&c, nm, RB_CONSUMER) == RB_OK
...
SKIP 6 stress checks (Part B): unit checks failed
Part C — liveness, restart, crash injection
FAIL starter.c:817  rb_peer_state(&c, T0) == PEER_NONE
...
SKIP 46 crash-injection checks (Part C): unit checks failed
(elapsed 0.00 s, 정보용)
16/131 passed
```

하네스 규칙:

- Part 마다 단일 프로세스 단위 테스트가 다 통과해야 스레드 스트레스·fork 시나리오가 돈다 (시작 코드가 매달리지 않게; 건너뛴 체크는 실패로 센다).
- 단위 테스트는 생산자 핸들과 소비자 핸들을 **같은 이름의 서로 다른 매핑** 으로 연다 (`c.h != p.h` 를 체크) — 프로세스 두 개와 같은 조건이다.
- 크래시 시나리오는 **감독자(부모) + 생산자 자식 + 소비자 자식** 구조다. 자식은 모두 `alarm(5)` 워치독을 달고, 부모는 `reap()` (WNOHANG 폴링, 시간 초과면 SIGKILL 후 회수)으로만 기다린다. 결과는 fork 전에 만든 `MAP_SHARED|MAP_ANON` 보고 영역에 모은다.
- shm 이름은 `/OS-A12.<pid>.<n>` (16 바이트 안팎, 한도 31), 마지막 체크가 쓴 이름 전부가 `shm_open` → 실패인지 확인한다.
- `SIGKILL` 로 죽은 자식은 crash report(.ips)를 남기지 않는다 (`~/Library/Logs/DiagnosticReports` 확인 — 이 레슨 이름의 파일 없음).

## 4. 힌트

<details><summary>힌트 1 — magic 은 왜 마지막인가, 크기는 어디서 믿나</summary>

여는 쪽은 만드는 쪽이 초기화를 끝냈는지 알 방법이 magic 하나뿐이다. 나머지 필드를 다 쓴 뒤 `atomic_store_explicit(&magic, RB_MAGIC, memory_order_release)`, 여는 쪽은 acquire 로 읽으면 그 앞의 일반 쓰기가 다 보인다. 크기는 `fstat` 이 아니라 **헤더의 total_size** 를 믿고, fstat 값(매핑 길이)은 "그만큼 매핑돼 있나" 의 상한 검사에만 쓴다 — 이 Mac 은 1000 바이트로 만들어도 fstat 이 16384 를 돌려준다.
</details>

<details><summary>힌트 2 — seqlock 쓰기의 fence 위치</summary>

쓰기: `마커 = 2p+1 (relaxed)` → `atomic_thread_fence(release)` → 본문 relaxed store 들 → `마커 = 2p+2 (release)`. 첫 fence 가 없으면 본문 store 가 "쓰는 중" 마커보다 먼저 보일 수 있고, 읽는 쪽 두 번째 마커 확인이 그것을 놓친다. 읽기: `m1 = 마커 (acquire)` → 본문 relaxed load → `atomic_thread_fence(acquire)` → `m2 = 마커 (relaxed)` → `m1 == m2 == 2p+2` 일 때만 유효 (LLAT-M02 seqlock 과 같은 모양, Boehm 2012).
</details>

<details><summary>힌트 3 — 마커 하나로 세 가지를 구분한다</summary>

위치 p 에 대해 기대값 `done = 2p+2`. 마커 `== done` → 게시됨. `< done` → 아직 안 썼거나(0, 이전 바퀴의 짝수 값), 지금 쓰는 중(2p+1). `> done` → 다음 바퀴(p+cap 이후)가 이미 이 칸을 차지 = **덮어쓰임(OVERRUN)**. 위치를 마커에 넣었기 때문에 "몇 바퀴째인가" 를 따로 저장할 필요가 없다.
</details>

<details><summary>힌트 4 — 유실은 위치가 아니라 msg_seq 로 센다</summary>

DROP_NEWEST 는 버린 메시지가 링에 아예 안 들어가므로 위치에는 빈틈이 없다. 생산자가 msg_seq 를 "시도할 때마다" 올리면 소비자는 `msg_seq − next_msg` 로 빈틈을 정확히 센다. gen 이 바뀐 직후의 첫 메시지는 기준점이 없으니 세지 않고 `resyncs++`.
</details>

<details><summary>힌트 5 — 죽음을 알았으면 한 번 더 본다</summary>

생산자가 메시지를 **게시한 직후** 죽을 수 있다. "비어 있음 → 상대 확인 → 죽었음" 사이에 그 메시지가 게시됐을 수 있으니, 죽음을 확인한 뒤 슬롯을 한 번 더 읽고 그래도 없을 때만 `RB_PEER_DEAD`. 파이프에서 자식이 끝난 뒤 EOF 까지 읽어 내는 것과 같은 이치다.
</details>

<details><summary>힌트 6 — 재시작한 생산자의 위치</summary>

tail 은 "게시 → tail 갱신" 사이에 죽으면 하나 뒤처진다. 새 생산자는 tail 에서 시작해 `slot_read(t) == SLOT_OK` 인 동안 t 를 올린다 (roll-forward). 쓰다 만 칸(홀수 마커, 체크섬 불일치)은 OK 가 아니므로 거기서 멈추고 그 칸을 다시 쓴다. 이어서 보낼 msg_seq 는 마지막 OK 슬롯의 msg_seq + 1.
</details>

## 5. 풀이

### Part A — 헤더: 무엇을 고정하고 무엇을 검사하나

| 필드 | 왜 | 실패 시 |
|---|---|---|
| `magic` (offset 0) | "이 메모리가 우리 링이고 **초기화가 끝났다**". 맨 마지막 release store | `ENOTREADY` → 잠시 후 재시도 |
| `version` (+4), `hdr_size` (+6) | 모든 버전에서 위치가 같은 **ABI 고정 접두사**. 낯선 버전도 여기까지는 읽을 수 있어야 "모른다" 라고 말할 수 있다 | `EVERSION` |
| `slot_size`, `capacity`, `total_size`, `policy` | 같은 버전이라도 다르게 컴파일된 빌드(슬롯 512 B)를 잡는다. 헤더가 스스로 크기를 말한다 | `ELAYOUT` |
| `total_size ≤ mapped` | 헤더가 거짓말해도(손상, 악의) 매핑 밖을 읽지 않는다 | `ELAYOUT` |
| `features` | v1 은 0. 하위 호환 확장용 (연습 1: 필수/선택 비트) | — |

macOS 의 shm 이 이 설계를 강제하는 두 가지 (위 probe 실측): **(1)** `fstat` 크기가 페이지(16 KB) 단위로 올림되므로 "fstat 크기 == 링 크기" 로 버전을 판단하면 틀린다 — 2432 바이트 링도 16384 로 보인다. 크기의 진실은 헤더의 `total_size`. **(2)** 두 번째 `ftruncate` 는 같은 크기여도 `EINVAL` — 그래서 `rb_create` 는 이미 있는 객체를 재사용할 때 `ftruncate` 를 건너뛰고 fstat 크기가 충분한지만 본다 ("헤더 쓰다 죽은 생산자의 재시작" 이 바로 이 경로).

헤더 초기화 중 죽으면? 시나리오 `hdr_mid`: P1 이 `version`·`hdr_size` 만 쓰고 SIGKILL → 부모의 `rb_attach` → `RB_ENOTREADY` (magic 이 0 이므로 나머지 필드를 **아예 믿지 않는다**) → P2 의 `rb_create` 가 EEXIST → 크기 충분, 검증 실패 → 다시 초기화 → 5 개 전송 후 정상 종료 → 부모가 붙어서 5 개 수신. 죽은 생산자가 남긴 메시지도 받는다 (`slot_read` 가 먼저, 생존 확인은 비었을 때만).

### Part A — 슬롯: seqlock 마커 + 체크섬, 그리고 크래시 지점

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">slot_publish(pos=p) 의 순서와 크래시 지점 — 각 지점에서 죽으면 소비자는 무엇을 보나</text>
  <line x1="20" y1="60" x2="720" y2="60" stroke="currentColor"/>
  <rect x="20" y="45" width="90" height="30" class="f-warn" stroke="currentColor"/>
  <text x="26" y="65" fill="currentColor" font-family="monospace" font-size="12">마커=2p+1</text>
  <rect x="125" y="45" width="70" height="30" class="f-mut" stroke="currentColor"/>
  <text x="131" y="65" fill="currentColor" font-family="monospace" font-size="12">fence</text>
  <rect x="210" y="45" width="130" height="30" class="f-mut" stroke="currentColor"/>
  <text x="216" y="65" fill="currentColor" font-family="monospace" font-size="12">payload ½ | ½</text>
  <rect x="355" y="45" width="120" height="30" class="f-mut" stroke="currentColor"/>
  <text x="361" y="65" fill="currentColor" font-family="monospace" font-size="12">meta, msg_seq</text>
  <rect x="490" y="45" width="70" height="30" class="f-mut" stroke="currentColor"/>
  <text x="496" y="65" fill="currentColor" font-family="monospace" font-size="12">csum</text>
  <rect x="575" y="45" width="90" height="30" class="f-ok" stroke="currentColor"/>
  <text x="581" y="65" fill="currentColor" font-family="monospace" font-size="12">마커=2p+2</text>
  <rect x="675" y="45" width="45" height="30" class="f-acc" stroke="currentColor"/>
  <text x="680" y="65" fill="currentColor" font-family="monospace" font-size="12">tail</text>
  <text x="112" y="100" fill="currentColor" font-size="12">① claimed</text>
  <text x="262" y="100" fill="currentColor" font-size="12">② half</text>
  <text x="472" y="100" fill="currentColor" font-size="12">③ seq|csum</text>
  <text x="560" y="100" fill="currentColor" font-size="12">④</text>
  <text x="660" y="100" fill="currentColor" font-size="12">⑤</text>
  <text x="718" y="100" fill="currentColor" font-size="12">⑥</text>
  <rect x="20" y="115" width="700" height="72" class="f-mut" stroke="currentColor"/>
  <text x="30" y="135" fill="currentColor">①~④ 에서 죽음: 마커가 홀수(2p+1) → 소비자에겐 "아직 없음". 찢어진 내용이 보일 길이 없다.</text>
  <text x="30" y="155" fill="currentColor">재시작 P2: slot_read(p) ≠ OK → roll-forward 0, 이 칸을 다시 씀 → 메시지 p 는 gen 2 로 1 번.</text>
  <text x="30" y="175" fill="currentColor">⑤ (게시 후 tail 전): 소비자는 마커로 이미 받음. P2 는 roll-forward 1 → 다시 안 보냄.</text>
  <rect x="20" y="197" width="700" height="52" class="f-ok" stroke="currentColor"/>
  <text x="30" y="217" fill="currentColor">⑥ (다 보낸 뒤): 평범한 죽음. 힌트 = p+1. 어느 지점이든 소비자 결과 = 0..39 정확히 한 번, 순서대로.</text>
  <text x="30" y="237" fill="currentColor">tail 이 뒤처져도 안전: 소비자는 슬롯 마커를 보고 읽는다 (tail 은 OVERRUN 점프용 힌트).</text>
  <rect x="20" y="259" width="700" height="62" class="f-warn" stroke="currentColor"/>
  <text x="30" y="279" fill="currentColor">BUG 조기 게시: 마커=2p+2 를 먼저 쓰고 payload 절반에서 죽음 → 마커는 "게시됨" 인데 내용은 반쪽.</text>
  <text x="30" y="299" fill="currentColor">체크섬 불일치 → CORRUPT, torn_rejected 1, 받지 않음. P2 복구도 체크섬을 봐서 이 칸을 다시 쓴다.</text>
  <text x="30" y="316" fill="currentColor">→ 순서가 맞으면 필요 없어 보이지만: 틀린 쓰기·비일관 캐시·DMA·손상의 마지막 방어선.</text>
</svg>
```

seqlock 이 왜 맞는가 (C11 기준): 읽는 쪽이 본문 워드 하나라도 "다음 쓰기" 의 값을 읽었다면, 그 store 는 쓰는 쪽의 release fence 뒤에 있으므로 읽는 쪽 acquire fence 와 동기화되고, 그 결과 쓰는 쪽의 `마커=2p+1` 이 읽는 쪽 `m2` 로드보다 happens-before 다 → `m2 ≠ m1`. 본문을 **원자적 relaxed 워드** 로 읽고 쓰기 때문에 C11 의미의 데이터 레이스가 없고, TSan 도 조용하다 (일반 `memcpy` 로 했다면 TSan 이 레이스로 보고한다 — 형식상으로도 UB). 이 증명 모양은 Boehm, "Can Seqlocks Get Along with Programming Language Memory Models?" (MSPC 2012) 의 fence 버전이다.

체크섬은 FNV-1a 64 비트로 (meta, msg_seq, payload) 를 덮는다. 우연히 맞을 확률은 2⁻⁶⁴ 수준이라 "찢어진 걸 받아들일" 확률로는 충분하지만, 악의적 위조는 못 막는다 (암호 MAC 이 아님 — 같은 사용자 프로세스끼리의 신뢰 경계 안이라는 가정). 하드웨어였다면 CRC32(C) 가 흔하다 (Cortex-M 의 CRC 주변장치).

Part A 실측 테스트에서 비트 하나를 뒤집으면 `rb_recv(…, 0)` 이 두 번 연속 `RB_CORRUPT` 를 돌려주지만 `torn_rejected == 1` (위치당 한 번만 셈), `pos` 는 그대로 3 — 손상된 메시지를 건너뛰지도 받지도 않고 **판단을 호출자에게** 넘긴다 (생산자가 살아 있으면 손상, 죽었으면 재시작이 다시 쓸 것).

### Part B — 가득 참 정책

| 정책 | 생산자 (가득 참) | 소비자 | 맞는 용도 | 상대가 죽으면 |
|---|---|---|---|---|
| BLOCK | 백오프 대기, 제한 시간 → `TIMEOUT`, 소비자 사망 → `PEER_DEAD` | 무손실, 순서 | 명령·설정·제어 메시지 | 생산자가 감지하고 정책 결정 (여기선 재시도 = 재시작 대기) |
| DROP_NEWEST | 새 것을 버리고 `dropped++` | gap 으로 유실 수를 앎 | 로그·텔레메트리 (오래된 맥락 보존) | 생산자는 계속 버리기만 한다 (막히지 않음) |
| OVERWRITE_OLDEST | 절대 안 기다림, 가장 오래된 칸을 덮음 | seqlock 으로 덮어쓰기 감지 → 최신 창으로 점프 | 최신 상태가 중요한 센서·카메라 프레임 | 소비자 사망과 무관 |

**BLOCK 의 대기 = 폴링.** macOS 에는 공개 futex 가 없어서 (OS-A01 은 리눅스 futex) 대기는 `cpu_relax` 64 번 → `sched_yield` 64 번 → 1 µs 부터 두 배씩 최대 1 ms `nanosleep` 백오프다. 공개 대안은 `os_sync_wait_on_address` (SDK 26.4 의 `os/os_sync_wait_on_address.h`, `__API_AVAILABLE(macos(14.4))`, 프로세스 간이면 `OS_SYNC_WAIT_ON_ADDRESS_SHARED`) — 연습 4 에서 도어벨로 구현했다. 그 아래에는 비공개 `__ulock_wait/__ulock_wake` 가 있다고 알려져 있으나 SDK 헤더에는 없다 (`sys/ulock.h` 없음 확인) — 제품 코드에서 쓰지 않는다.

**OVERWRITE 의 재동기화 셈.** 위치 `pos` 에서 마커가 `> 2·pos+2` 면 생산자가 최소 `pos+cap` 까지 왔다. `t = tail` 이면 위치 `t−cap+1 … t−1` 은 게시돼 있고(칸 `cap−1` 개), 위치 `t` 는 지금 쓰는 중일 수 있는 칸(= `t−cap` 과 같은 칸)이다. 그래서 `np = max(t−cap+1, pos+1)` — 한 칸 여유를 둬서 바로 다시 덮일 칸을 피한다. 단위 테스트: cap 4, 0 을 받은 뒤 1..10 을 보내면 tail 11 → `np = 8`, 8·9·10 을 받고 `lost = 7`(1..7), `skipped = 7`, `overruns = 1`.

**스레드 스트레스의 불변식** (스케줄과 무관하게 매번 성립해야 하는 것만 체크): 끝 표시 msg N 은 버려지면 재시도해 반드시 전달.

| 정책 | 체크 |
|---|---|
| BLOCK | `received == N+1`, `lost == 0`, 첫 메시지 0 |
| DROP_NEWEST | 첫 메시지 0 (링이 빈 상태에서 시작), `lost == dropped`, `received + dropped == N+1` (끝 표시의 재시도는 dropped 에서 뺀다) |
| OVERWRITE | `received + lost + first == N+1`, `skipped == lost + first` (첫 수신 전에 덮였을 수 있으므로 `first` 를 따로 센다) |
| 공통 | 내용 검증(`msg_ok`) 0 건 실패, 순서 역전 0, `torn_rejected == 0` |

실측 (solution, load 16.6): `stress DROP_NEWEST n=200000 received 21847 lost 178154 dropped 178154`, `stress OVERWRITE_OLDEST n=200000 received 82637 lost 115119 … skipped 117364` → `first = 2245` 로 불변식 성립. 숫자 자체는 실행마다 크게 달라진다 (DROP 의 received 가 2만~19만) — 소비자가 1024 개마다 일부러 쉬고, 부하가 높기 때문.

**세대 울타리(fencing).** `rb_send` 는 매번 `prod_gen == 내 gen` 을 확인한다. 소비자나 감독자가 생산자를 "죽었다" 고 오판해 새 생산자를 띄웠는데 옛 생산자가 사실 멈춰 있었을 뿐이라면, 깨어난 옛 생산자는 `RB_FENCED` 를 받고 손을 뗀다 (분산 시스템의 fencing token 과 같은 아이디어). 확인과 게시 사이의 창은 남는다 — Q7 참고.

### Part C — 생존 판단: 하트비트 + kill, 그리고 좀비

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">rb_peer_state(now): 상대(pid, gen, hb) 를 보고 판단 — 대기 루프에서 1 ms 에 한 번</text>
  <rect x="10" y="35" width="150" height="34" class="f-mut" stroke="currentColor"/>
  <text x="20" y="57" fill="currentColor">pid == 0 ?</text>
  <text x="170" y="57" fill="currentColor">예 → NONE (아직 없음 / 정상 분리) — 죽음 아님</text>
  <rect x="10" y="80" width="150" height="34" class="f-mut" stroke="currentColor"/>
  <text x="20" y="102" fill="currentColor">(pid, gen) 새 것?</text>
  <text x="170" y="102" fill="currentColor">예 → 감시 재시작 (last_hb, last_change = now) → ALIVE</text>
  <rect x="10" y="125" width="150" height="34" class="f-warn" stroke="currentColor"/>
  <text x="20" y="147" fill="currentColor">kill(pid,0) ESRCH?</text>
  <text x="170" y="147" fill="currentColor">예 → GONE (확실). EPERM 은 "있다" (남의 프로세스일 수도)</text>
  <rect x="10" y="170" width="150" height="34" class="f-ok" stroke="currentColor"/>
  <text x="20" y="192" fill="currentColor">hb 바뀜?</text>
  <text x="170" y="192" fill="currentColor">예 → last_change = now → ALIVE</text>
  <rect x="10" y="215" width="150" height="34" class="f-warn" stroke="currentColor"/>
  <text x="20" y="237" fill="currentColor">now − last ≥ stale?</text>
  <text x="170" y="237" fill="currentColor">예 → STALE (죽음 의심) / 아니오 → ALIVE</text>
  <rect x="470" y="125" width="260" height="124" class="f-mut" stroke="currentColor"/>
  <text x="480" y="145" fill="currentColor" font-weight="bold">kill 이 "있다" 고 말하는 경우</text>
  <text x="480" y="165" fill="currentColor">• 좀비: 부모가 아직 waitpid 안 함</text>
  <text x="480" y="185" fill="currentColor">• pid 재사용: 다른 프로세스가 그 번호</text>
  <text x="480" y="205" fill="currentColor">• SIGSTOP·디버거·심한 부하로 멈춤</text>
  <text x="480" y="225" fill="currentColor">→ 셋 다 하트비트 정체로만 잡힌다</text>
  <text x="480" y="243" fill="currentColor">(멈춤은 "죽음" 이 아니라 오판 위험)</text>
  <text x="10" y="278" fill="currentColor">테스트 (가짜 시계, stale=100): T0 ALIVE, +99 ALIVE, +100 STALE, hb+1 → ALIVE, gen 변경 → 새 감시,</text>
  <text x="10" y="298" fill="currentColor">좀비(waitid WNOWAIT): kill(z,0)==0, ALIVE → +100 STALE → waitpid 후 GONE. pid 0 → NONE.</text>
  <text x="10" y="316" fill="currentColor">시계를 인자로 받으니 "정확히 stale 에서" 를 시간 측정 없이 체크할 수 있다.</text>
</svg>
```

**왜 카운터인가.** 하트비트를 "마지막으로 살아 있던 시각" 으로 저장하면 두 프로세스가 같은 시계를 같은 단위로 읽는다는 가정이 들어간다. 같은 Mac 안에서는 대체로 맞지만, 듀얼코어 MCU 처럼 코어마다 타이머가 다르거나, 컨테이너·VM 경계, 시계 점프(REALTIME)에서 깨진다. 카운터는 "바뀌었나" 만 보므로 관찰자 **자기 시계** 로만 잰다 — 시계 동기화가 필요 없다.

**stale 임계값.** 너무 짧으면 부하 스파이크·스왑·디버거 정지를 죽음으로 오판한다 (이 기계의 load 가 19 까지 올라갔다). 하트비트 주기의 수십 배 + 최악 스케줄 지연으로 잡는다. 레슨 기본값은 500 ms, 벤치의 좀비 측정은 100 ms. 확실한 죽음은 하트비트를 기다리지 않는다: **감독자가 회수하면 `kill` → ESRCH 로 즉시**, 더 좋게는 kqueue `EVFILT_PROC`/`NOTE_EXIT` (연습 3, 비자식도 가능 — probe 확인) 나 리눅스 pidfd (OS-M18). pid 재사용은 (pid, 프로세스 시작 시각) 신원으로 막는다 (연습 2, `proc_pidinfo(PROC_PIDTBSDINFO)` 의 `pbi_start_tvsec/usec`; 좀비에서는 ESRCH 로 실패함을 실측).

### Part C — 재시작: 세대, roll-forward, 소비자 쪽 at-least-once

`rb_start` (생산자) 의 결정은 세 가지다. ① `prod_gen` fetch_add → 메시지마다 gen 이 찍혀 소비자가 재시작을 안다 (`resyncs++`) + 옛 생산자 울타리. ② **tail 을 믿지 않고 마커로 복구**: tail 에서 시작해 OK 인 슬롯을 지나간다. ⑤ 지점(게시 후 tail 전)에서 죽은 경우 이것이 없으면 어떻게 되는지 돌연변이로 확인했다 — roll-forward 한 줄을 지우면 P2 의 `pos = 10` 인데 소비자는 이미 위치 10 을 읽어 `head = 11`, BLOCK 검사 `pos − head` 가 **부호 없는 뺄셈으로 2⁶⁴−1** 이 되어 "영원히 가득" — `s2 exit 22, consumer exit 11, cnt[10]=1 cnt[11]=0` (P2 는 시간 초과 반복, 소비자는 11 을 못 받음). ③ 이어 보낼 msg_seq = 마지막 OK 슬롯 + 1 — 응용이 "어디까지 보냈나" 를 따로 저장하지 않아도 된다 (공유 메모리가 곧 진실).

소비자 쪽은 `head` 가 커밋 지점이다. 이 레슨의 소비자 자식은 `manual_commit` 으로 **처리(보고 영역 기록) → 커밋** 순서를 쓴다. 처리 후 커밋 전에 죽으면 재시작한 C2 는 `head` 에서 다시 시작해 그 메시지를 한 번 더 받는다 = **at-least-once**. 커밋을 먼저 하면 반대로 처리 전에 죽은 메시지가 사라진다 = at-most-once. "정확히 한 번" 은 처리 결과와 커서를 **한 번의 원자적 전환** 으로 같이 커밋해야 얻는다 (연습 5).

### Part C — 크래시 주입 실측

```text
Part C — liveness, restart, crash injection
  hdr_mid: P1 killed in header init → attach ENOTREADY → P2 re-init → 5 msgs delivered
  P-crash slot_claimed     P1 SIGKILL, consumer saw death (reason GONE), P2 gen 2 hint 10 rolled 0, torn_rejected 0
  P-crash payload_half     P1 SIGKILL, consumer saw death (reason GONE), P2 gen 2 hint 10 rolled 0, torn_rejected 0
  P-crash seq_before_csum  P1 SIGKILL, consumer saw death (reason GONE), P2 gen 2 hint 10 rolled 0, torn_rejected 0
  P-crash before_publish   P1 SIGKILL, consumer saw death (reason GONE), P2 gen 2 hint 10 rolled 0, torn_rejected 0
  P-crash before_tail      P1 SIGKILL, consumer saw death (reason GONE), P2 gen 2 hint 11 rolled 1, torn_rejected 0
  P-crash after_send       P1 SIGKILL, consumer saw death (reason GONE), P2 gen 2 hint 11 rolled 0, torn_rejected 0
  P-crash BUG_early_pub    P1 SIGKILL, consumer saw death (reason GONE), P2 gen 2 hint 10 rolled 0, torn_rejected 1
  C-crash before_commit    C1 SIGKILL, producer saw death (reason GONE), C2 first msg 10, msg 10 processed 2 x
  C-crash after_commit     C1 SIGKILL, producer saw death (reason GONE), C2 first msg 11, msg 10 processed 1 x
```

시나리오마다 체크하는 것: P1 이 정말 SIGKILL 로 죽었고 P2·소비자는 exit 0, 소비자가 죽음을 봤고 이유가 GONE 또는 STALE, **0..39 각각 정확히 1 번**, 순서 역전 0, 내용 검증 실패(찢어진 수락) 0, `resyncs == 1`, `lost == 0`, P2 gen 2, 힌트·rolled·"메시지 10 을 쓴 gen" 이 지점별 기댓값과 일치, 조기 게시 버그에서만 `torn_rejected == 1`. 소비자 사망은 BLOCK 8 칸 링이 차서 생산자가 기다리는 동안 감지한다. 감독자는 소비자가 죽음을 **기록한 뒤** 재시작하므로 "감지" 가 매번 체크 가능하다 (재시작이 먼저 오면 소비자는 새 gen 만 보고 죽음을 못 볼 수도 있다 — 그것도 실제로는 정상 동작).

이유가 항상 GONE 인 것은 감독자가 죽은 자식을 1 ms 안에 회수하기 때문이다. 같은 부모가 소비자이면서 생산자의 부모라면(회수 안 함) 좀비라서 STALE 경로로만 잡힌다 — 벤치의 두 번째 줄.

### 측정: 비용과 감지 지연 (demo_bench.c)

<!-- code: demo_bench.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o bench && ./bench 5
```

실제 출력 두 번 (64 B 메시지, 프로세스 2 개, 각 항목 5 회 중앙값):

```text
OS-A12 bench: 5 runs, 64 B messages | uptime: 17:37  up 13 days, 17:58, 14 users, load averages: 19.14 19.15 14.41
thru  shm ring (BLOCK, cap 1024) :     6.28 M msgs/s  (median of 5, N = 1000000)
thru  pipe (write/read 64 B)     :     1.28 M msgs/s
rtt   shm ring  p50    0.67 us  p99    20.88 us  (20000 rounds, median of 5)
rtt   pipe      p50    7.88 us  p99   217.96 us
detect reaped -> kill ESRCH       median    1.02 ms  (min 1.00, max 1.15)  [of which kill->waitpid 0.78 ms]
detect zombie -> hb STALE(100ms)  median  100.85 ms  (min 100.52, max 101.97)
detect kqueue NOTE_EXIT           median    0.77 ms  (min 0.50, max 0.90)
shm names leaked: 0
OS-A12 bench: 5 runs, 64 B messages | uptime: 17:37  up 13 days, 17:59, 14 users, load averages: 18.13 18.93 14.39
thru  shm ring (BLOCK, cap 1024) :     5.74 M msgs/s  (median of 5, N = 1000000)
thru  pipe (write/read 64 B)     :     1.41 M msgs/s
rtt   shm ring  p50    0.67 us  p99     6.92 us  (20000 rounds, median of 5)
rtt   pipe      p50    6.79 us  p99    73.62 us
detect reaped -> kill ESRCH       median    1.18 ms  (min 1.04, max 1.44)  [of which kill->waitpid 0.67 ms]
detect zombie -> hb STALE(100ms)  median  100.95 ms  (min 100.26, max 101.32)
detect kqueue NOTE_EXIT           median    0.68 ms  (min 0.52, max 2.26)
shm names leaked: 0
```

읽는 법 (load 18~19 의 공유 기계 — 비율만):

1. **처리량 4~5 배.** 헤더·마커·체크섬(64 B FNV-1a 바이트 루프)·하트비트를 다 넣어도 메시지당 시스템 콜이 0 인 링이 메시지당 write+read 시스템 콜 2 번인 파이프보다 4.1~4.9 배 빠르다. 메시지마다 도는 체크섬 바이트 루프가 고정 비용의 큰 몫일 것으로 보지만 프로파일은 하지 않았다 (줄이려면 워드 단위 해시나 CRC32 명령).
2. **왕복 10 배, 꼬리 10 배.** shm p50 0.67 µs 는 **양쪽이 스핀 중** 이라서 나온다 (백오프 첫 64 회가 `yield` 명령 스핀) — CPU 두 개를 태우는 값이다. 파이프는 잠들었다 깨어나는 비용(6.8~7.9 µs)을 낸다. 스핀 없이 잠들면 shm 도 깨움 비용을 낸다: 연습 4 의 os_sync 도어벨 왕복은 22 µs (같은 부하). OS-B14 가 "작은 메시지에서는 알림 비용이 전부" 라고 한 것과 같은 결론이다.
3. **감지 지연.** 회수된 경우 ~1 ms 중 0.7~0.8 ms 는 SIGKILL 후 커널이 프로세스를 정리해 `waitpid` 가 돌아오기까지이고, 나머지는 `peer_dead_now` 의 1 ms 간격 제한이다. kqueue NOTE_EXIT 은 그보다 조금 빠르다 (0.5~0.9 ms, 폴링 없음). 좀비는 정확히 stale(100 ms) + 1 ms 안쪽. 하트비트는 **바닥(fallback)** 이고 1 차 감지는 OS 통지여야 한다.
4. 지연 측정은 `clock_gettime_nsec_np(CLOCK_UPTIME_RAW)` 를 쓴다. macOS 의 `CLOCK_MONOTONIC` 은 `clock_getres` 가 1000 ns 이고 실제로 1 µs 단위로 움직인다 (확인함) — 첫 측정에서 RTT 가 1.00/3.00 µs 로 뭉개져 바꿨다.

### 다른 시스템은 어떻게 하나 (문서 기준, 직접 검증하지 않음)

| 시스템 | 대응하는 아이디어 |
|---|---|
| 리눅스 robust mutex (OS-M17) | 락 주인의 죽음을 커널(robust futex list)이 `EOWNERDEAD` 로 알려 줌. 락이 있는 설계에 한정, macOS 없음 |
| 리눅스 pidfd (OS-M18) | 프로세스 사망을 fd 로 통지, pid 재사용에 안전 — 이 레슨의 kqueue NOTE_EXIT 자리 |
| Eclipse iceoryx | 공유 메모리 zero-copy 미들웨어. 중앙 데몬(RouDi)이 참여 프로세스 생존을 감시하고 죽은 프로세스의 자원을 회수한다고 문서화돼 있다 (ROS-A05 에서 다룰 예정) |
| Android FMQ | AIDL/HIDL Fast Message Queue. "synchronized" (단일 리더, 가득 차면 실패) 와 "unsynchronized" (여러 리더, 쓰기가 덮어쓰고 리더가 overflow 를 감지) 두 종류 — BLOCK/DROP 과 OVERWRITE 에 대응 |
| Chromium | 공유 메모리 영역(`base::ReadOnlySharedMemoryRegion` 등)과 Mojo data pipe 로 프로세스 간 대용량 전달, 상대 사망은 Mojo 메시지 파이프의 연결 끊김 통지로 안다 |
| 듀얼코어 MCU (STM32H7 HSEM + 공유 SRAM, nRF5340 IPC 주변장치 + 공유 RAM, OpenAMP RPMsg) | 공유 RAM 링 + doorbell 인터럽트 + **하트비트 카운터** + 워치독. Cortex-M7 D-cache 는 M4 와 일관(coherent)하지 않아 공유 영역을 MPU 로 non-cacheable 지정하거나 clean/invalidate 가 필요 (PLAT-M05 에서 다룰 예정) |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_san && ./so_san
cc -std=c11 -g -O1 -fsanitize=thread solution.c -o so_tsan && ./so_tsan
```

실제 출력 (load 16.6, 앞부분 — Part C 는 5. 풀이의 실측과 같은 실행):

```text
OS-A12 robust shm IPC (pid 40474)
Part A — header / slot seqlock / checksum / gap
Part B — BLOCK / DROP_NEWEST / OVERWRITE_OLDEST
  stress BLOCK            n=200000 received 200001 lost 0 dropped 0 overruns 0 skipped 0 (정보용)
  stress DROP_NEWEST      n=200000 received 21847 lost 178154 dropped 178154 overruns 0 skipped 0 (정보용)
  stress OVERWRITE_OLDEST n=200000 received 82637 lost 115119 dropped 0 overruns 9113 skipped 117364 (정보용)
Part C — liveness, restart, crash injection
  ...
(elapsed 0.44 s, 정보용)
131/131 passed
```

추가로 5 회 연속 `131/131 passed` (real 0.33~0.61 s), 다른 cwd(`/tmp`)에서도 `131/131 passed`, 개발 중 12 회 연속 반복도 전부 통과. ASan+UBSan: `131/131 passed`, 보고 없음, 2.2 s. TSan: `131/131 passed`, 보고 없음, exit 0 (12.6 s — 스트레스는 SAN 빌드에서 2 만 개로 자동 축소). TSan 은 프로세스 경계를 넘는 레이스는 못 보므로 스레드 스트레스(같은 매핑을 두 스레드가 공유)가 그 역할을 대신한다.

## 7. 자주 하는 실수

1. **magic 을 먼저 쓰고 나머지를 채움** → 여는 쪽이 capacity 0 이나 쓰레기 total_size 를 믿고 매핑 밖을 읽는다. 고침: magic 은 `memory_order_release` 로 **마지막**, 여는 쪽은 acquire 로 먼저 확인하고 다른 필드를 본다. 재초기화 땐 magic 을 0 으로 먼저 지운다 (`rb_create` 재사용 경로).
2. **fstat 크기로 레이아웃을 판단** → macOS 는 페이지 단위 올림이라 `st_size == total` 이 거의 항상 거짓, 리눅스에선 우연히 맞아 이식할 때 터진다. 고침: 크기는 헤더의 `total_size`, fstat 는 `total ≤ mapped` 상한 검사.
3. **마커를 본문보다 먼저 "게시됨" 으로** (조기 게시, 또는 첫 release fence 누락) → 소비자가 반쪽 메시지를 받는다. 이 레슨에서는 체크섬이 잡아 `torn_rejected 1` 로 끝났지만, 체크섬이 없으면 조용한 데이터 손상이다. 고침: 마커 홀수 → fence(release) → 본문 → 마커 짝수(release).
4. **`kill(pid,0) == 0` 이면 살아 있다** → 좀비(부모 미회수), pid 재사용, SIGSTOP 을 놓친다. 실측: 좀비에 `kill` 은 0. 고침: 하트비트 정체를 같이 보고, 가능하면 OS 통지(kqueue NOTE_EXIT / pidfd) 와 (pid, 시작 시각) 신원.
5. **stale 임계값을 하트비트 주기 몇 배로 짧게** → 부하 스파이크에 "죽음" 오판 → 새 생산자 시작 → 생산자 둘. 고침: 넉넉한 임계값 + 세대 울타리(`RB_FENCED`) + 확실한 신호(ESRCH)가 있을 때만 즉시 재시작.
6. **재시작한 생산자가 tail 을 믿음** → "게시 후 tail 전" 크래시에서 `pos − head` 가 부호 없는 언더플로로 "영원히 가득" (돌연변이 실측: P2 exit 22, 소비자 exit 11). 고침: 마커로 roll-forward, 그리고 위치 뺄셈 전에 `pos ≥ head` 가 불변식인지 생각할 것.
7. **처리 전에 커밋** → 처리 중 죽으면 메시지가 사라진다 (at-most-once). 처리 후 커밋은 중복(at-least-once). 고침: 요구사항에 맞게 고르고, 정확히 한 번이 필요하면 결과+커서 원자 커밋(연습 5).
8. **이름 정리 누락 / 두 번째 ftruncate** → `/OS-A12.*` 가 재부팅까지 남고, 재시작한 생산자의 `ftruncate` 가 EINVAL 로 실패해 "만들 수 없음" 이 된다. 고침: 시험 이름에 pid, 끝에서 `shm_unlink` 와 누수 체크, 재사용 경로에선 크기만 확인.

## 8. 면접 Q&A

<details><summary>Q1. 공유 메모리 채널에서 상대 프로세스가 죽은 걸 어떻게 아나요?</summary>

공유 메모리는 커널이 상대 상태를 알려 주지 않으니 세 겹으로 한다. ① **OS 통지**: 감독자(systemd/launchd 또는 부모)가 SIGCHLD·waitpid 로 즉시 알고, 비자식이면 macOS kqueue `EVFILT_PROC`/`NOTE_EXIT`, 리눅스 pidfd. ② **`kill(pid,0)` == ESRCH**: 회수된 뒤엔 확실하다. 다만 좀비·pid 재사용·EPERM 은 "있다" 로 보인다. ③ **하트비트 카운터 정체**: 위 두 가지가 놓치는 경우(좀비, 재사용, 멈춤)의 바닥. 실측 지연: 회수됨 → ESRCH ~1 ms, NOTE_EXIT 0.5~0.9 ms, 좀비 → stale 100 ms. 또 "죽었다" 를 안 뒤에도 슬롯을 한 번 더 읽는다 — 죽기 직전 게시한 메시지가 있을 수 있다.
</details>

<details><summary>Q2. 하트비트는 왜 타임스탬프가 아니라 카운터죠? 주기와 임계값은?</summary>

타임스탬프는 쓰는 쪽과 읽는 쪽이 같은 시계·단위를 쓴다는 가정이 든다. 카운터는 "바뀌었나" 만 보고 경과 시간은 관찰자 자기 시계로 잰다 → 코어마다 타이머가 다른 MCU, VM, 시계 점프에도 안전하다. 주기는 일하는 경로(보내기/받기마다) + 한가할 때 타이머로 박동. 임계값은 주기 × 수십 + 최악 스케줄 지연 (이 기계는 load 19 — 수백 ms 정지가 가능). 짧으면 오판, 길면 늦은 감지 — 그래서 하트비트는 1 차 감지가 아니라 **보조 수단**이다.
</details>

<details><summary>Q3. 헤더 버전은 어떻게 설계하나요? v2 가 나오면?</summary>

위치가 영원히 고정된 접두사(magic, version, hdr_size)를 둔다 — 모르는 버전도 거기까지는 읽고 "모른다" 고 말할 수 있어야 한다. magic 은 마지막 release store 로 "초기화 끝" 표시를 겸한다. 크기는 헤더가 스스로 말하고(total_size), fstat 은 상한 검사만 (macOS 는 페이지 올림). 호환은 major/minor 로: major 가 다르면 거절, 같은 major 에서는 헤더가 **뒤로만** 자라고(hdr_size), 기능 비트를 필수(모르면 거절)/선택(모르면 무시)으로 나눈다 (연습 1). 배포 순서도 정한다: 읽는 쪽을 먼저 올리고 쓰는 쪽을 나중에.
</details>

<details><summary>Q4. seqlock 마커가 있는데 체크섬이 왜 또 필요하죠?</summary>

seqlock 은 **올바른 순서로 쓰는 단일 작성자** 와 coherent 메모리를 가정한다. 체크섬은 그 가정이 깨질 때의 방어선이다: 순서를 틀린 작성자(조기 게시 버그 — 이 레슨에서 `torn_rejected 1` 로 잡힘), 캐시가 일관되지 않은 다른 코어(Cortex-M7↔M4)나 DMA, 메모리 손상, 재시작한 생산자가 같은 위치를 다시 쓸 때 마커 값이 같은 ABA. 메시지마다 payload 를 한 번 더 훑는 비용이 들지만, 신뢰 경계가 약할수록(다른 코어, DMA, 다른 팀의 프로세스) 남긴다. 단 FNV 는 MAC 이 아니다 — 악의적 위조까지 막으려면 다른 이야기(권한 분리, 서명).
</details>

<details><summary>Q5. 가득 찼을 때 BLOCK / DROP_NEWEST / OVERWRITE_OLDEST 는 어떻게 고르나요?</summary>

데이터의 의미로 고른다. 명령·설정처럼 하나라도 잃으면 안 되면 BLOCK (단, 제한 시간과 상대 사망 감지가 있어야 데드락이 아니다). 로그·이벤트처럼 오래된 맥락이 중요하고 생산자를 절대 막으면 안 되면 DROP_NEWEST + 유실 카운터. 센서·카메라처럼 **최신 값만 의미 있으면** OVERWRITE — 생산자는 소비자 상태와 완전히 독립(소비자가 죽어도 무관)이고, 소비자는 seqlock 으로 덮어쓰기를 감지해 점프한다. 어느 쪽이든 유실 수를 **정확히** 세어 노출해야 운영에서 쓸 수 있다 (여기선 msg_seq gap = 생산자 dropped).
</details>

<details><summary>Q6. 생산자가 죽었다 재시작하면 소비자는 어떻게 재동기화하나요?</summary>

새 생산자는 gen 을 올리고, tail 을 믿지 않고 슬롯 마커로 위치를 복구한다 (게시됐지만 tail 이 안 오른 칸은 roll-forward, 쓰다 만 칸은 다시 씀). 다음 msg_seq 는 마지막 유효 슬롯에서 이어 간다. 소비자는 자기 head 그대로 계속 읽으면 되고(그 사이 칸은 모두 유효하거나 다시 쓰일 칸), 메시지의 gen 이 바뀐 것을 보고 `resyncs++` 하며 seq 기준점을 새로 잡는다. 크래시 7 지점 모두에서 0..39 가 정확히 한 번씩 왔다. 소비자 재시작은 반대로 head 에서 재개 → 커밋 순서에 따라 at-least/at-most-once.
</details>

<details><summary>Q7. (꼬리) 이 설계가 틀리는 경우는?</summary>

- **오판된 죽음 → 생산자 둘**: 멈췄던(SIGSTOP, 디버거, 스왑) 옛 생산자가 깨어나면, `prod_gen` 확인과 실제 게시 사이 창에서 새 생산자와 같은 칸을 쓸 수 있다. 울타리는 창을 줄일 뿐 없애지 못한다. 진짜로 막으려면 칸 소유를 CAS 로 잡거나(생산자 단일성 깨짐 = MPSC 설계), 옛 프로세스를 **확실히 죽인 뒤**(kill + 회수) 재시작해야 한다 — 그래서 감독자가 재시작을 맡는다.
- **pid 네임스페이스·샌드박스**: 리눅스 컨테이너끼리는 pid 가 의미가 없고, 샌드박스에서 `kill` 이 EPERM 이면 "있음" 으로만 보인다 → 하트비트만 남는다.
- **비일관 메모리**: 다른 코어의 캐시가 coherent 하지 않으면 C11 원자성 가정 자체가 깨진다 (MPU non-cacheable + 배리어 필요).
- **악의적 상대**: 헤더·인덱스를 믿고 그대로 쓰면 안 된다 — 모든 인덱스는 mask 하고(`pos & mask`), 길이는 검사한다(이 코드는 함). 그 이상은 권한 분리 문제.
- **이름 기반 shm**: 누구든 같은 이름을 먼저 만들 수 있다 — 리눅스면 memfd + SCM_RIGHTS(OS-M16)로 이름 없이 fd 를 넘기는 편이 안전하다.
</details>

<details><summary>Q8. (꼬리) 생산자 1 : 소비자 N (브로드캐스트) 이라면 어떻게 바꾸겠나요?</summary>

head 를 소비자마다 하나씩(소비자 테이블: pid, gen, hb, head) 두고, BLOCK 이면 생산자는 **가장 느린 살아 있는** 소비자의 head 를 본다 — 죽은 소비자 하나가 전체를 막지 않도록 하트비트가 정체된 소비자는 테이블에서 퇴출(evict)하고, 재접속하면 최신 위치에서 시작시킨다. 실시간성이 더 중요하면 OVERWRITE 로 바꿔 생산자가 소비자를 아예 모르게 한다 (Android FMQ unsynchronized 와 같은 모양 — 문서 기준). 큰 페이로드라면 슬롯에 데이터 대신 공유 풀의 청크 인덱스 + 참조 카운트를 넣고, 죽은 소비자가 쥔 참조를 감독자가 회수해야 한다 (iceoryx 류, ROS-A05).
</details>

<details><summary>Q9. 리눅스였다면 무엇을 다르게 하나요? robust mutex 를 쓰면 되지 않나요?</summary>

락 기반 설계라면 robust mutex(OS-M17)가 "락 주인의 죽음" 을 커널이 알려 주니 편하다. 하지만 ① 락 없는 SPSC 링에는 락이 없고, ② `EOWNERDEAD` 는 **락을 쥔 채** 죽었을 때만 알려 주며(락 밖에서 죽은 생산자는 여전히 모름), ③ 조건변수는 robust 하지 않다. 리눅스에서는 이 레슨의 프로토콜에 pidfd(OS-M18) 로 정확한 사망 통지, `FUTEX_WAIT/WAKE`(shared, OS-A01) 로 폴링 없는 대기, memfd + SCM_RIGHTS 로 이름 없는 공유를 더하는 것이 자연스럽다. 감독은 systemd `Restart=` + watchdog(ELX-B13).
</details>

## 9. 추가 연습

### 연습 1 — minor 버전 호환 (헤더 확장 + 기능 비트)

같은 major(1)에서 헤더를 512 B 로 키우고(확장 필드 하나) `features` 에 비트를 붙인 "v1.1 작성자" 를 받아들이는 `compat_validate` / `compat_attach` 를 작성하라. 하위 16 비트는 필수(모르면 거절), 상위 16 비트는 선택(무시). 엄격한 v1.0 `rb_attach` 는 거절해야 한다.

<details><summary>답 보기</summary>

검사 순서: magic → major(version) → **모르는 필수 비트** (`features & 0xFFFF & ~known`) → `hdr_size ≥ sizeof(rb_hdr_t)` 이고 128 의 배수 → 나머지 레이아웃. 슬롯 시작은 `base + hdr_size` (이미 `rb_fill_handle` 이 hdr_size 를 쓰므로 그대로 동작). 테스트: 엄격 리더 `ELAYOUT`, 호환 리더는 슬롯이 +512 에 있고 v1.1 링으로 메시지 왕복, 확장 필드 0x1234 보존, 모르는 필수 비트 → `EVERSION`, 그 비트를 아는 리더 → OK. 확장 필드에 생산자 시작 시각(연습 2)을 넣는 것이 자연스러운 다음 걸음이다.
</details>

### 연습 2 — pid 재사용에 강한 신원 (pid, 시작 시각)

`proc_pidinfo(pid, PROC_PIDTBSDINFO, …)` 의 `pbi_start_tvsec/pbi_start_tvusec` 로 프로세스 신원을 만들고 `identity_state(pid, start_us)` 를 작성하라. 같은 pid 라도 시작 시각이 다르면 GONE(재사용), 조회 실패면 GONE.

<details><summary>답 보기</summary>

실측: 자기 자신 → ALIVE, 시작 시각 −1 µs → GONE (재사용 흉내). 좀비에 대해서는 `kill(z,0) = 0` 인데 `proc_pidinfo` 가 실패한다(errno 3 = ESRCH) — 출력 `zombie: kill(pid,0)=0, proc_pidinfo fails (errno 3)`. 즉 신원 확인은 좀비를 "없음" 으로 본다 (하트비트를 기다릴 필요가 없다). 리눅스에서는 `/proc/<pid>/stat` 의 22 번째 필드 starttime (proc(5)) 이 같은 역할이고, 더 좋은 답은 pidfd 다.
</details>

### 연습 3 — kqueue NOTE_EXIT 로 폴링 없는 사망 통지

`watch_exit(kq, pid)` 와 `wait_exit(kq, ms, &status)` 를 작성하라 (`EVFILT_PROC`, `NOTE_EXIT | NOTE_EXITSTATUS`, `EV_ONESHOT`). 이미 사라진 pid 등록은 실패해야 한다.

<details><summary>답 보기</summary>

자식이 30 ms 뒤 `_exit(3)` → 통지가 **회수 전에** 오고 `out.data` 에 wait status (`WEXITSTATUS == 3`) — 실측 31.4 ms. SIGKILL 한 자식은 `WTERMSIG == SIGKILL`. 회수된 pid 등록 → `-1 / ESRCH`: "pid 를 읽고 → 등록" 사이에 죽으면 등록이 실패하므로 호출자는 ESRCH 를 "이미 죽음" 으로 처리해야 한다. 비자식(손자)도 감시 가능함은 probe 에서 확인했다. 대기 루프에 kqueue fd 를 넣으면 shm 링의 하트비트 폴링을 1 차 감지에서 뺄 수 있다.
</details>

### 연습 4 — os_sync_wait_on_address 도어벨로 BLOCK 대기

`bell_t { seq, waiters, wakes }` 를 `MAP_SHARED|MAP_ANON` 에 두고 `bell_ring` / `bell_wait(last, ms)` 를 `os_sync_wait_on_address_with_timeout(…, OS_SYNC_WAIT_ON_ADDRESS_SHARED, OS_CLOCK_MACH_ABSOLUTE_TIME, ns)` 로 작성하라. 깨움 시스템 콜은 기다리는 쪽이 있을 때만.

<details><summary>답 보기</summary>

waiters 증가 → seq 재확인 → 커널 대기 / seq 증가 → waiters 확인 → 깨움. 서로 다른 변수에 대한 store→load 두 쌍(SB 모양)이라 **둘 다 seq_cst** 여야 "둘 다 상대 store 를 못 보는" 경우가 없어 깨움을 잃지 않는다 (OS-A01 futex 와 같은 계약: 커널이 값을 원자적으로 비교한 뒤 재운다). 실측: 시간 초과 경로 21.3 ms(20 요청), 프로세스 간 핑퐁 2000 회 **22.2 µs/왕복**, 깨움 시스템 콜 3931/4000 회 — 상대가 이미 잠들어 있으니 거의 매번 필요하다. 스핀 링의 0.67 µs 와의 차이가 "CPU 를 태우는 값" 이다. 실제로는 짧게 스핀한 뒤 잠드는 하이브리드를 쓴다.
</details>

### 연습 5 — 정확히 한 번 처리 (결과 + 커서 원자 커밋)

응용 상태 `{sum, applied, last_msg}` 레코드 두 개와 "현재" 인덱스 하나를 공유 메모리에 두고, 새 레코드를 다 쓴 뒤 인덱스를 release store 로 바꿔 커밋하라. 링 커밋은 그 뒤. 재시작한 소비자는 `msg_seq ≤ last_msg` 를 건너뛴다. 레코드 쓰는 중 / 전환 후 링 커밋 전에 죽여 보라.

<details><summary>답 보기</summary>

세 경우 모두 합계가 기대값 1192851 과 같고 `applied 40` — 정확히 한 번. "레코드 쓰는 중" 사망은 전환 전이라 반영이 안 보이고, 링 head 도 안 올라 C2 가 그 메시지를 다시 받아 반영한다 (중복 건너뜀 0). "전환 후 커밋 전" 사망은 반영됐는데 head 가 안 올라 C2 가 다시 받지만 커서가 막는다 (**중복 건너뜀 1**). 핵심은 결과와 커서가 **같은 원자 단위** 에 있다는 것 — 데이터베이스의 트랜잭션 로그, Kafka 의 "오프셋을 결과와 같은 트랜잭션에 저장" 과 같은 원리다. 단일 쓰기 주체라 더블 버퍼 + 인덱스 전환으로 충분하다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice      # solution.c 와 같은 폴더에서
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_san && ./pr_san
```

실제 출력:

```text
연습 1 — minor 호환
연습 2 — pid 재사용에 강한 신원
  zombie: kill(pid,0)=0, proc_pidinfo fails (errno 3), start read before exit: no
연습 3 — kqueue NOTE_EXIT
  exit notice after 31.4 ms (child slept 30 ms, 정보용)
연습 4 — os_sync 도어벨
  timeout path 21.3 ms (asked 20), 2000 round trips, 22.2 us/RTT, wake syscalls 3931 of 4000 rings (정보용)
연습 5 — 정확히 한 번 처리
  no crash             sum 1192851 (want 1192851), applied 40, duplicates skipped 0
  crash mid-record     sum 1192851 (want 1192851), applied 40, duplicates skipped 0
  crash after switch   sum 1192851 (want 1192851), applied 40, duplicates skipped 1
40/40 passed
```

3 회 연속 `40/40 passed` (0.26 s), `/tmp` 에서 실행해도 통과, ASan+UBSan `40/40 passed` 보고 없음.

## 10. 핵심 정리

- 공유 메모리는 상대의 죽음을 알려 주지 않는다 → **프로토콜에** 버전·하트비트·세대·체크섬을 넣는다.
- 헤더: 고정 접두사(magic·version·hdr_size) + 스스로 말하는 크기(total_size). **magic 은 마지막 release**. macOS shm 은 fstat 크기가 페이지 올림, 두 번째 ftruncate 는 EINVAL.
- 슬롯: 마커 2p+1 → fence(release) → 본문(원자 워드) → 체크섬 → 마커 2p+2(release). 읽기는 m1 acquire → 복사 → fence(acquire) → m2. `> 2p+2` 면 덮어쓰임. 체크섬은 순서가 틀린 쓰기의 마지막 방어선.
- 정책: BLOCK(제한 시간 + 상대 사망 감지), DROP_NEWEST(유실 = msg_seq gap = dropped), OVERWRITE(점프 `max(t−cap+1, pos+1)`).
- 생존: OS 통지(회수 → ESRCH, kqueue NOTE_EXIT, pidfd) 가 1 차, **하트비트 카운터 정체가 바닥**. 좀비·pid 재사용·멈춤은 kill 로 못 본다. 실측 ~1 ms / 0.7 ms / stale.
- 재시작: gen+1, tail 대신 마커로 roll-forward (없으면 `pos−head` 언더플로로 영원히 가득), 옛 생산자는 `RB_FENCED`. 소비자는 커밋 순서가 곧 at-least/at-most-once, 정확히 한 번은 결과+커서 원자 커밋.
- 비용 (load 18~19): 64 B 처리량 shm 5.7~6.3 M/s vs pipe 1.3~1.4 M/s, 스핀 RTT 0.67 µs vs 6.8~7.9 µs, 잠드는 도어벨 22 µs.

## 참고자료

- POSIX `shm_open` — <https://pubs.opengroup.org/onlinepubs/9799919799/functions/shm_open.html>, `kill` (sig 0 = 존재 확인, ESRCH/EPERM) — <https://pubs.opengroup.org/onlinepubs/9799919799/functions/kill.html>, `waitid` (WNOWAIT) — <https://pubs.opengroup.org/onlinepubs/9799919799/functions/waitid.html>
- macOS SDK 26.4 헤더 (직접 읽음): `sys/posix_shm.h` (PSHMNAMLEN 31), `os/os_sync_wait_on_address.h` (macOS 14.4+, SHARED 플래그, ETIMEDOUT), `sys/event.h` (EVFILT_PROC, NOTE_EXIT, NOTE_EXITSTATUS), `sys/proc_info.h` (`pbi_start_tvsec`), `libproc.h` (`proc_pidinfo`)
- ISO C11 (N1570) 7.17.4 fence, 7.17.5 lock-free 와 address-free NOTE — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- Hans-J. Boehm, "Can Seqlocks Get Along with Programming Language Memory Models?", MSPC 2012
- OSTEP 30장 Condition Variables (생산자/소비자 bounded buffer), 31장 Semaphores, 42장 Crash Consistency (커밋 순서·재생의 같은 생각), 44장 Data Integrity and Protection (체크섬) — operating-system-three-easy-pieces/book-md/
- Linux man-pages: pidfd_open(2), proc(5) (`/proc/pid/stat` starttime), futex(2), pthread_mutexattr_setrobust(3) — <https://man7.org/linux/man-pages/>
- Eclipse iceoryx 문서 (RouDi, 공유 메모리 zero-copy) — <https://iceoryx.io/>; Android FMQ 문서 — <https://source.android.com/docs/core/architecture/hidl/fmq>; Chromium Mojo 문서 — <https://chromium.googlesource.com/chromium/src/+/main/mojo/README.md> (모두 문서 기준, 직접 검증 안 함)
- STM32H7 HSEM (RM0399), nRF5340 IPC 주변장치 (Product Specification), OpenAMP RPMsg — 듀얼코어 메일박스 배경 (문서 기준)
- 관련 레슨: DS-A01 (SPSC 링·memory_order), OS-B14 (IPC 비교, shm 상대 사망 시 무한 대기), OS-M17 (robust mutex), OS-M18 (pidfd), OS-M16 (SCM_RIGHTS), OS-A01 (futex), LLAT-M02 (seqlock), C-A03 (acquire/release), ELX-B13 (systemd watchdog·재시작), OS-A11 (크래시 후 재시작 데몬)
