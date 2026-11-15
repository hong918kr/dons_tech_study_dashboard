---
id: OS-M17
time: 90분
updated: 2026-11-11
---
# OS-M17 프로세스 간 공유 동기화: PROCESS_SHARED 뮤텍스와 robust mutex

## 학습 목표

- `shm_open` → `ftruncate` → `mmap(MAP_SHARED)` 로 공유 메모리 객체를 만들고, 그 안에 `PTHREAD_PROCESS_SHARED` 뮤텍스·조건변수를 **한 번만** 초기화해서 서로 다른 프로세스가 같은 락을 쓰게 만든다 (이름 수명, `O_EXCL`, 크기 확인, `shm_unlink` 까지).
- 락을 쥔 프로세스가 `SIGKILL` 로 죽었을 때 일반 뮤텍스는 **영원히 잠긴다**는 것을 `pthread_mutex_timedlock` → `ETIMEDOUT` 으로 (멈추지 않고) 보이고, robust mutex 는 `EOWNERDEAD` → 불변식 복구 → `pthread_mutex_consistent` 로 살아난다는 것을 보인다.
- `consistent` 를 빼먹으면 `ENOTRECOVERABLE` 이 되어 **모든 프로세스가 영원히** 그 락을 못 쓴다는 것, 그때 허용되는 연산은 `destroy` 뿐이라는 것을 테스트로 확인한다.
- "락은 살아났는데 데이터는?" — undo 저널로 찢어진 갱신을 되돌리고, `pthread_cond_timedwait` 도 `EOWNERDEAD` 를 돌려준다는 것, 조건변수는 robust 가 아니라는 것(죽은 대기자가 있으면 glibc `pthread_cond_destroy` 가 멈춤)을 실측한다.
- 커널의 robust futex list(`set_robust_list`)가 이 모든 것을 어떻게 가능하게 하는지, 그리고 macOS 에는 왜 이 API 자체가 없는지 설명한다.

## 1. 문제

"공유 메모리 위의 뮤텍스를 쥔 프로세스가 크래시하면 어떻게 되나요?" — 임베디드 리눅스 면접에서 IPC 를 이야기하다 보면 반드시 나오는 꼬리질문이고, 실제 제품에서도 그대로 터진다. 카메라·IMU 센서 데몬이 `/dev/shm` 링 버퍼에 쓰고, 로거·텔레메트리·제어 프로세스가 읽는 구조에서 센서 데몬이 락을 쥔 채 segfault 나면, systemd 가 데몬을 재시작해도 **락은 죽은 프로세스가 쥔 그대로**라서 시스템 전체가 멈춘다. 워치독이 보드를 리셋할 때까지. 스레드 하나의 락(OS-B05)과 조건변수 bounded buffer(OS-B06)는 이미 했다. 이 레슨은 같은 도구를 **프로세스 경계 너머**로 옮기고, 그 순간 새로 생기는 문제 — 주인의 죽음 — 를 다룬다. futex 자체(사용자 공간 단어 + 커널 대기 큐)는 OS-A01 에서 했으니 여기서는 그 위에 얹힌 robust futex list 만 본다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">한 shm 객체를 두 프로세스가 각자 다른 주소로 mmap — 락·조건변수·데이터가 전부 그 안에</text>
  <rect x="15" y="40" width="190" height="120" class="f-ok" stroke="currentColor"/>
  <text x="25" y="60" fill="currentColor" font-weight="bold">생산자 (센서 데몬)</text>
  <text x="25" y="82" fill="currentColor" font-family="monospace" font-size="12">shm_open(name,O_RDWR)</text>
  <text x="25" y="100" fill="currentColor" font-family="monospace" font-size="12">mmap(MAP_SHARED)</text>
  <text x="25" y="118" fill="currentColor" font-family="monospace" font-size="12">r = 0xfe4d69cc3000</text>
  <text x="25" y="146" fill="currentColor">ring_put → lock/signal</text>
  <rect x="535" y="40" width="190" height="120" class="f-ok" stroke="currentColor"/>
  <text x="545" y="60" fill="currentColor" font-weight="bold">소비자 (로거)</text>
  <text x="545" y="82" fill="currentColor" font-family="monospace" font-size="12">shm_open(name,O_RDWR)</text>
  <text x="545" y="100" fill="currentColor" font-family="monospace" font-size="12">mmap(MAP_SHARED)</text>
  <text x="545" y="118" fill="currentColor" font-family="monospace" font-size="12">r = 0xfe4d69cc2000</text>
  <text x="545" y="146" fill="currentColor">ring_get → wait/signal</text>
  <rect x="235" y="40" width="270" height="250" class="f-mut" stroke="currentColor"/>
  <text x="245" y="60" fill="currentColor" font-weight="bold">/dev/shm/OS-M17_&lt;pid&gt;_a (tmpfs)</text>
  <rect x="250" y="70" width="240" height="26" class="f-acc" stroke="currentColor"/>
  <text x="258" y="88" fill="currentColor" font-family="monospace" font-size="12">mu  (PSHARED + ROBUST)</text>
  <rect x="250" y="100" width="240" height="26" class="f-acc" stroke="currentColor"/>
  <text x="258" y="118" fill="currentColor" font-family="monospace" font-size="12">not_empty, not_full (PSHARED)</text>
  <rect x="250" y="130" width="240" height="26" fill="none" stroke="currentColor"/>
  <text x="258" y="148" fill="currentColor" font-family="monospace" font-size="12">m = {head,tail,count,sums}</text>
  <rect x="250" y="160" width="240" height="26" class="f-warn" stroke="currentColor"/>
  <text x="258" y="178" fill="currentColor" font-family="monospace" font-size="12">j = 직전 m, j_active (undo)</text>
  <rect x="250" y="190" width="240" height="26" fill="none" stroke="currentColor"/>
  <text x="258" y="208" fill="currentColor" font-family="monospace" font-size="12">buf[16]</text>
  <text x="250" y="240" fill="currentColor">주소는 프로세스마다 달라도</text>
  <text x="250" y="258" fill="currentColor">물리 페이지는 하나 → futex 키 =</text>
  <text x="250" y="276" fill="currentColor">(공유 페이지, 오프셋), PRIVATE 아님</text>
  <path d="M205 100 L235 100" stroke="currentColor"/><path d="M535 100 L505 100" stroke="currentColor"/>
  <text x="15" y="190" fill="currentColor">락 안에 포인터를 두면 안 된다:</text>
  <text x="15" y="208" fill="currentColor">상대 프로세스에선 다른 주소.</text>
  <text x="15" y="226" fill="currentColor">인덱스·오프셋만 저장.</text>
  <text x="535" y="190" fill="currentColor">죽어도 shm 은 남는다:</text>
  <text x="535" y="208" fill="currentColor">이름은 shm_unlink 까지,</text>
  <text x="535" y="226" fill="currentColor">페이지는 마지막 munmap 까지.</text>
  <text x="10" y="318" fill="currentColor">주소: VM 실측 (만든 쪽 vs fork 후 이름으로 다시 연 자식) — 한 페이지 차이로 다르다.</text>
</svg>
```

**환경 (이 레슨의 모든 실측)**: Lima VM `elinux` (vz, aarch64, 6 vCPU, 12 GiB) · Ubuntu 26.04.1 LTS · kernel `7.0.0-38-generic` · glibc 2.43 (`getconf GNU_LIBC_VERSION`) · gcc 15.2.0 (`gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0`) · clang 21.1.8 · strace 설치됨 · `/dev/shm` = tmpfs 5.9G. VM `uptime` load average 0.08 (데모 시점). 호스트 Apple M6, macOS 27.2, `Apple clang version 21.0.0`. **리눅스 전용 레슨**이다: 맥에는 `pthread_mutexattr_setrobust` 가 없다 (5. 풀이 끝의 맥 대조 참고).

### Part A — 공유 링 만들기·열기·지우기

- **TODO(A1)** `shring_create(name, robust)` — `shm_open(O_CREAT|O_EXCL|O_RDWR, 0600)` → `ftruncate(sizeof(shring_t))` → `mmap(MAP_SHARED)` → fd 닫기. 뮤텍스 속성 `PTHREAD_PROCESS_SHARED` (+ `PTHREAD_MUTEX_ROBUST`), 조건변수 2 개 `PTHREAD_PROCESS_SHARED` + `CLOCK_MONOTONIC`. `magic` 은 **맨 마지막에** 쓴다. 실패하면 만든 것을 지우고 `NULL`.
- **TODO(A2)** `shring_open(name)` — `shm_open(O_RDWR)` → `fstat` 으로 크기 확인 → `mmap` → `magic` 확인. 절대 다시 init 하지 않는다.
- **TODO(A3)** `shring_destroy(r, name)` — `pthread_cond_destroy` ×2, `pthread_mutex_destroy`(결과를 돌려줌), `munmap`, `shm_unlink`.
- 주어진 것: `ring_put` / `ring_get` (용량 16, 조건변수 대기, undo 저널, 크래시 주입 지점 `CR_LOCKED / CR_MID / CR_COMMITTED`), `ring_check` (불변식 위반 수).
- 테스트: `/dev/shm/<name>` 크기·권한 0600, 같은 이름 재생성 → `EEXIST`, 자식이 **이름으로 따로 열어** 락을 잡고 쓴 pid 가 부모에게 보임, 생산자 프로세스 + 소비자 프로세스가 xorshift32 값 20000 개를 주고받아 **순서·값·합계 일치**, destroy 후 `shm_open` → `ENOENT`.

### Part B — 락을 쥔 프로세스를 SIGKILL

- 일반 뮤텍스: 자식이 lock 후 `raise(SIGKILL)`. 부모의 `timedlock(150 ms)` 과 **제3의 프로세스**의 `timedlock(150 ms)` 둘 다 `ETIMEDOUT`. (무한 `lock` 이면 영원히 멈춘다 — 테스트는 절대 그렇게 부르지 않는다.)
- **TODO(B1)** `ring_repair(r)` — undo 저널이 열려 있으면(`j_active`) `m = j` 로 되돌리고 1, 저널 없이 불변식이 깨져 있으면 빈 링으로 초기화하고 2, 멀쩡하면 0.
- **TODO(B2)** `ring_lock(r, ms)` — `pthread_mutex_timedlock`. `EOWNERDEAD` 면 `recover_locked(r, 0)` (주어짐: repair → 조건변수 broadcast → `pthread_mutex_consistent` → `recoveries++`) 후 0. 나머지 오류는 그대로.
- 테스트: robust 에서 "lock 만 잡고 죽음" → 복구 1 회, 되돌림 0 회. "tail 만 올리고(CR_MID) 멈춘 자식을 부모가 SIGKILL" → 찢어진 상태(`ring_check > 0`)가 실제로 보이고, `ring_lock` 후 메타데이터가 **바이트 단위로** 사망 전과 같다. `consistent` 없이 unlock → 부모·다른 프로세스·`ring_lock` 모두 `ENOTRECOVERABLE`.

### Part C — 센서 데몬 재시작: 조건변수와 이어 쓰기

- **TODO(C1)** `ring_wait(r, cv, dl)` — 뮤텍스를 쥔 채 `min(지금+50 ms, dl)` 까지 `pthread_cond_timedwait`. `EOWNERDEAD` → `recover_locked(r, 1)` → 0. 조각 시간 초과면 0 (호출자가 조건을 다시 본다), 전체 마감이 지났으면 `ETIMEDOUT`.
- **TODO(C2)** `producer_run(r, n, crash_seq, crash)` — **shm 에 커밋된 `tail` 부터** `n-1` 까지 `VAL(seq)` 를 넣는다. `seq == crash_seq` 인 put 하나만 크래시 모드로.
- 주어진 것: `supervise()` — 생산자를 띄우고, SIGKILL 로 죽으면 다음 크래시 계획으로 재시작 (systemd `Restart=on-failure` 흉내).
- 테스트: 소비자가 `not_empty` 에서 자는 중에 생산자가 ① 커밋 후 signal 전에 죽음 → 소비자가 **자기 `cond_timedwait` 안에서** `EOWNERDEAD` 를 받아 회복하고 값 4242 를 받는다 ② 갱신 도중 죽음 → 되돌려져서 받을 값이 없음(`ETIMEDOUT`). 감시자 시나리오: 생산자를 seq 100(락 직후)·1200(갱신 도중)·2500(커밋 후) 에서 죽이고 3 번 재시작 → 소비자는 0..2999 를 **정확히 한 번씩 순서대로**, 복구 3 회·되돌림 1 회.

## 2. Problem (English)

> Two Linux processes share a bounded ring buffer in a POSIX shared-memory object. Implement:
>
> ```c
> shring_t *shring_create(const char *name, int robust); // shm_open+ftruncate+mmap, PROCESS_SHARED mutex/conds
> shring_t *shring_open(const char *name);                // map an existing ring, never re-initialize it
> int  shring_destroy(shring_t *r, const char *name);     // destroy, munmap, shm_unlink
> int  ring_repair(shring_t *r);                          // roll back a half-done update (undo journal)
> int  ring_lock(shring_t *r, int timeout_ms);            // 0 / ETIMEDOUT / ENOTRECOVERABLE; handles EOWNERDEAD
> int  ring_wait(shring_t *r, pthread_cond_t *cv, const struct timespec *deadline_mono);
> int  producer_run(shring_t *r, uint32_t n, uint32_t crash_seq, int crash_mode);
> ```
>
> Requirements: a process that dies (SIGKILL) while holding the lock must not wedge the others. With a plain
> process-shared mutex, show the lock stays held forever (use `pthread_mutex_timedlock`; tests must never hang).
> With `PTHREAD_MUTEX_ROBUST`, the next locker gets `EOWNERDEAD`, repairs the shared invariants, calls
> `pthread_mutex_consistent`, and continues. Unlocking without `consistent` must make every later lock fail with
> `ENOTRECOVERABLE`. A waiter blocked in `pthread_cond_timedwait` must also survive the owner's death, and a
> restarted producer must resume from the committed state in shared memory.
> Example: producer killed at seq 100 (after lock), 1200 (mid-update), 2500 (after commit, before signal);
> the consumer still receives 0..2999 exactly once, in order.
> Follow-ups: what does the kernel do at exit to make this work? Why can't a condition variable be robust?
> What do you do on an OS without robust mutexes?

## 3. 시작 코드

<!-- code: starter.c -->

**리눅스 전용** (`/* ENV: linux */`, 둘째 줄 `/* BUILD: -pthread */`). 맥 홈은 VM 에 **읽기 전용**으로 마운트되므로 VM 의 `/tmp/OS-M17/` 로 복사해서 빌드한다. 이 VM 의 glibc 2.43 에서는 `shm_open` 도 pthread 도 `-lrt` 없이 링크된다 (오래된 glibc 는 `-lrt`·`-lpthread` 가 필요했다).

```bash
limactl list                              # elinux 가 Running 인지
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/OS/OS-M17
limactl shell elinux -- bash -lc "mkdir -p /tmp/OS-M17 && cp $L/*.c /tmp/OS-M17/ && cd /tmp/OS-M17 &&
  gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o st && ./st"
```

시작 코드 실행 (실제 출력, 즉시 끝남 — `shring_create` 가 `NULL` 이면 그 Part 의 프로세스 테스트는 아예 띄우지 않는다):

```text
== Part A: shm_open + mmap 위의 PROCESS_SHARED 뮤텍스·조건변수 ==
FAIL r != NULL  (line 340)
== Part B: 락을 쥔 프로세스를 SIGKILL ==
FAIL r != NULL  (line 402)
FAIL r != NULL  (line 417)
== Part C: 조건변수 대기 중 주인 사망, 생산자 재시작 ==
FAIL r != NULL  (line 470)
PASS count_leftover_shm() == 0
1/5 passed
exit=1
```

하네스 규칙: 모든 자식은 `alarm(5)` watchdog 을 달고 시작하고, 부모는 `wait_child()` (WNOHANG 폴링 + 시간 초과 시 SIGKILL)로만 기다린다. 모든 락은 `timedlock`. shm 이름은 `/OS-M17_<pid>_<tag>` 이고 마지막 CHECK 가 `/dev/shm` 에 우리 pid 이름이 하나도 안 남았는지 센다.

## 4. 힌트

<details>
<summary>힌트 1 — 누가 init 하나: O_EXCL 과 magic</summary>

프로세스 간 뮤텍스의 가장 흔한 버그는 "두 번째 프로세스도 `pthread_mutex_init` 을 부른다" 다. 남이 쥐고 있는 락을 다시 init 하면 정의되지 않은 동작이다. 만드는 쪽은 `O_CREAT|O_EXCL` 로 "내가 처음" 을 보장받고, 여는 쪽은 `O_RDWR` 만 쓴다. 만드는 쪽이 init 을 끝내기 전에 여는 쪽이 들어올 수 있으니, 다 만든 뒤 **마지막에** `magic` 을 쓰고 여는 쪽은 그것을 확인한다. `ftruncate` 로 늘어난 부분은 0 으로 읽힌다.

</details>

<details>
<summary>힌트 2 — 속성 4 개</summary>

`pthread_mutexattr_setpshared(&ma, PTHREAD_PROCESS_SHARED)`, `pthread_mutexattr_setrobust(&ma, PTHREAD_MUTEX_ROBUST)`, `pthread_condattr_setpshared(&ca, PTHREAD_PROCESS_SHARED)`, `pthread_condattr_setclock(&ca, CLOCK_MONOTONIC)`. 이 함수들은 실패 시 `errno` 가 아니라 **오류 번호를 반환**한다. 속성 객체는 init 후 바로 destroy 해도 된다 (뮤텍스가 값을 복사해 둔다).

</details>

<details>
<summary>힌트 3 — EOWNERDEAD 는 "실패" 가 아니다</summary>

`pthread_mutex_timedlock` 이 `EOWNERDEAD` 를 돌려주면 **락은 이미 내 것**이다. 다만 "직전 주인이 고치다 죽었을 수 있다" 는 경고가 붙어 있다. 그래서 순서는: ① 데이터 불변식 복구(`ring_repair`) ② 죽은 주인이 못 보냈을 수 있는 signal 을 broadcast 로 대신 ③ `pthread_mutex_consistent` ④ 평소처럼 진행. ③ 없이 unlock 하면 그 뮤텍스는 영구히 `ENOTRECOVERABLE` 이다.

</details>

<details>
<summary>힌트 4 — 조건변수 대기도 EOWNERDEAD 를 돌려준다</summary>

`pthread_cond_timedwait` 은 돌아오기 전에 뮤텍스를 다시 잡는다. 그 순간 주인이 죽어 있으면 반환값이 `EOWNERDEAD` 다 (락은 쥔 상태). 그런데 죽은 생산자는 signal 을 못 보냈으니 소비자는 **스스로 깨어나야** 이것을 알 수 있다 → 긴 대기를 50 ms 조각으로 나눈다. 조각 시간 초과는 "가짜 깨어남" 과 똑같이 처리하면 된다: 0 을 돌려주고 호출자의 `while (조건)` 이 다시 본다. 조건변수의 시계가 `CLOCK_MONOTONIC` 이니 마감도 그 시계로.

</details>

<details>
<summary>힌트 5 — 재시작한 생산자는 어디서부터?</summary>

죽은 프로세스의 지역 변수(`seq`)는 사라졌다. 남은 진실은 shm 안의 커밋된 `m.tail` 뿐이다. `ring_lock` 으로 읽고(이 lock 이 바로 회복 지점일 수 있다) unlock 한 뒤 그 값부터 넣는다. 생산자가 하나뿐이라 읽은 뒤 tail 이 바뀔 일은 없다.

</details>

## 5. 풀이

### Part A — shm 객체의 수명과 초기화 순서

| 단계 | 호출 | 왜 / 주의 |
|---|---|---|
| 이름 만들기 | `shm_open("/OS-M17_<pid>_a", O_CREAT\|O_EXCL\|O_RDWR, 0600)` | 이름은 `/` 로 시작, 리눅스는 `/dev/shm` (tmpfs) 의 파일. `O_EXCL` 로 "init 은 나만" |
| 크기 | `ftruncate(fd, sizeof(shring_t))` | 새 객체는 크기 0 → mmap 해도 접근하면 SIGBUS. 늘어난 부분은 0 |
| 매핑 | `mmap(NULL, size, RW, MAP_SHARED, fd, 0)` 후 `close(fd)` | `MAP_PRIVATE` 면 쓰는 순간 COW 사본 → 공유 안 됨 |
| 초기화 | 속성 → `pthread_mutex_init`, `pthread_cond_init` ×2 → 마지막에 `magic` | 여는 쪽은 `magic` 을 보고 "준비 끝" 판단 |
| 열기 | `shm_open(O_RDWR)` → `fstat` 크기 확인 → `mmap` | **init 하지 않는다**. 크기가 다르면 버전 불일치 (OS-A12 의 헤더·버전 필드) |
| 정리 | destroy → `munmap` → `shm_unlink` | 이름은 unlink 까지 남는다. 크래시한 테스트가 `/dev/shm` 을 더럽히지 않게 pid 를 이름에 |

뮤텍스·조건변수를 공유 메모리에 두는 것만으로는 부족하고 **pshared 속성**이 필요한 이유: 리눅스에서 뮤텍스는 futex 이고, 기본(private) 뮤텍스는 `FUTEX_*_PRIVATE` 로 잠든다. private futex 의 키는 (프로세스 주소 공간, 가상 주소) 라서 다른 프로세스의 대기자를 못 찾는다 (OS-A01 에서 `PRIVATE` 대기자를 shared `FUTEX_WAKE` 로 못 깨우는 것을 실측했다). pshared 는 shared futex → 키 = (공유 페이지, 오프셋). 그래서 두 프로세스의 매핑 주소가 달라도 같은 대기 큐에서 만난다.

시계 두 개를 섞어 쓴다는 점도 짚어 둔다. POSIX `pthread_mutex_timedlock` 의 마감은 **`CLOCK_REALTIME`** 이다 (NTP 가 시간을 돌리면 늘거나 줄 수 있다). 조건변수는 `pthread_condattr_setclock(CLOCK_MONOTONIC)` 로 바꿀 수 있다. glibc 2.30 부터 `pthread_mutex_clocklock(m, CLOCK_MONOTONIC, &dl)` 도 있다 (이 VM 의 `pthread.h` 에서 `__USE_GNU` 아래 선언 확인) — 시간 점프가 걱정되는 데몬이면 이쪽.

Part A 실측 (solution, VM):

```text
== Part A: shm_open + mmap 위의 PROCESS_SHARED 뮤텍스·조건변수 ==
PASS r != NULL
PASS stat(path, &st) == 0 && st.st_size == (off_t)sizeof(shring_t) && (st.st_mode & 0777) == 0600
PASS shring_create(nm, 0) == NULL && errno == EEXIST
PASS exited_ok(s) && r->hello == (uint32_t)p
  producer exit=0 consumer exit=0  sum_put=42842606655438 sum_get=42842606655438 head=20000 tail=20000
PASS exited_ok(sp) && exited_ok(sc)
...
PASS shm_open(nm, O_RDWR, 0) < 0 && errno == ENOENT
```

용량 16 짜리 링에 20000 개를 흘렸으니 생산자와 소비자는 수천 번 서로를 조건변수로 재우고 깨웠다 — 그 대기·깨움이 전부 프로세스 경계를 넘는 shared futex 다.

### Part B — 주인이 죽으면: 일반 vs robust

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">robust mutex 상태 기계 (glibc + 리눅스 robust futex list)</text>
  <rect x="20" y="40" width="120" height="44" class="f-ok" stroke="currentColor"/>
  <text x="50" y="67" fill="currentColor">풀림 (0)</text>
  <rect x="230" y="40" width="160" height="44" class="f-ok" stroke="currentColor"/>
  <text x="240" y="60" fill="currentColor">잠김</text>
  <text x="240" y="77" fill="currentColor" font-family="monospace" font-size="12">word = 주인 TID</text>
  <path d="M140 52 L230 52" stroke="currentColor"/><text x="160" y="47" fill="currentColor" font-size="12">lock</text>
  <path d="M230 74 L140 74" stroke="currentColor"/><text x="155" y="92" fill="currentColor" font-size="12">unlock</text>
  <rect x="480" y="40" width="240" height="44" class="f-warn" stroke="currentColor"/>
  <text x="490" y="60" fill="currentColor">주인 사망: 커널이 robust list 처리</text>
  <text x="490" y="77" fill="currentColor" font-family="monospace" font-size="12">word = 0x40000000 (OWNER_DIED)</text>
  <path d="M390 62 L480 62" stroke="currentColor"/><text x="400" y="56" fill="currentColor" font-size="12">exit/kill</text>
  <rect x="480" y="130" width="240" height="60" class="f-acc" stroke="currentColor"/>
  <text x="490" y="150" fill="currentColor">다음 lock → EOWNERDEAD</text>
  <text x="490" y="168" fill="currentColor">락은 내 것, 상태는 "불일치"</text>
  <text x="490" y="184" fill="currentColor" font-family="monospace" font-size="12">__owner = INT_MAX (glibc 표시)</text>
  <path d="M600 84 L600 130" stroke="currentColor"/>
  <rect x="230" y="130" width="200" height="60" class="f-ok" stroke="currentColor"/>
  <text x="240" y="150" fill="currentColor">repair → broadcast</text>
  <text x="240" y="168" fill="currentColor">→ pthread_mutex_consistent</text>
  <text x="240" y="184" fill="currentColor">= 정상 "잠김" 으로</text>
  <path d="M480 160 L430 160" stroke="currentColor"/>
  <path d="M310 130 L310 84" stroke="currentColor"/>
  <rect x="480" y="230" width="240" height="60" class="f-warn" stroke="currentColor"/>
  <text x="490" y="250" fill="currentColor">consistent 없이 unlock</text>
  <text x="490" y="268" fill="currentColor">→ ENOTRECOVERABLE (영구, 모두)</text>
  <text x="490" y="284" fill="currentColor">허용: pthread_mutex_destroy 뿐</text>
  <path d="M600 190 L600 230" stroke="currentColor"/>
  <path d="M700 190 C 740 160, 740 100, 700 84" stroke="currentColor" stroke-dasharray="4 3" fill="none"/>
  <text x="626" y="120" fill="currentColor" font-size="12">복구 중 또 죽음</text>
  <rect x="20" y="130" width="180" height="90" class="f-mut" stroke="currentColor"/>
  <text x="30" y="150" fill="currentColor" font-weight="bold">일반 뮤텍스 (STALLED)</text>
  <text x="30" y="170" fill="currentColor">주인 사망 → 아무 일 없음</text>
  <text x="30" y="188" fill="currentColor" font-family="monospace" font-size="12">word=1, __owner=죽은TID</text>
  <text x="30" y="206" fill="currentColor">lock = 영원히, timedlock = ETIMEDOUT</text>
  <text x="20" y="250" fill="currentColor">EOWNERDEAD 는 단 한 명에게:</text>
  <text x="20" y="268" fill="currentColor">그가 consistent 하면 이후는 평범한 lock.</text>
  <text x="20" y="286" fill="currentColor">그도 죽으면 다음 사람이 또 EOWNERDEAD.</text>
  <text x="20" y="318" fill="currentColor">값 0x40000000 · INT_MAX 는 아래 demo_owner_death.c 의 실측 (glibc 내부 필드).</text>
</svg>
```

**일반 뮤텍스 (POSIX 이름 `PTHREAD_MUTEX_STALLED`)**: 리눅스 man 페이지 표현 그대로 "주인이 unlock 없이 죽으면 뮤텍스는 그 뒤로도 잠겨 있고 이후의 lock 은 무한히 막힌다". 커널은 이 락이 누구 것인지 모른다 — futex 단어는 그냥 int 다. 실측:

```text
== Part B: 락을 쥔 프로세스를 SIGKILL ==
PASS killed_by(s, SIGKILL)
  normal: parent timedlock=Connection timed out  other process=Connection timed out
PASS e1 == ETIMEDOUT && e2 == ETIMEDOUT
  normal: pthread_mutex_destroy on dead-locked mutex = 16 (Device or resource busy)
```

부모도, 제3의 프로세스도 못 잡는다. 잠긴 채로 `pthread_mutex_destroy` 하면 glibc 는 `EBUSY` 를 돌려줬다 (POSIX 로는 잠긴 뮤텍스 destroy 가 정의되지 않은 동작이니 이 숫자에 기대지 말 것). 일반 뮤텍스에서 복구하는 유일한 방법은 **shm 객체 전체를 버리고 새로 만드는 것** — 그러려면 모든 참여 프로세스가 동의해야 한다. 실제로는 모두 재시작이다.

**robust 뮤텍스**: 커널이 주인의 죽음을 안다. 방법은 robust futex list 다.

1. glibc 는 스레드마다 "내가 쥔 robust 뮤텍스 목록" 의 머리를 사용자 공간에 두고, 스레드 시작 때 `set_robust_list(head, 24)` 로 커널에 주소를 알려 준다. robust 뮤텍스를 잡을 때마다 그 목록에 끼우고, 풀 때 뺀다 (syscall 없이, 사용자 공간 연결 리스트). 잡는 도중에 죽는 경우를 위해 `list_op_pending` 칸도 있다.
2. 스레드가 어떤 이유로든 끝나면 (exit, SIGKILL, execve) 커널이 그 목록을 걸어 다니며, futex 단어의 TID 가 죽는 스레드인 항목마다 단어에 `FUTEX_OWNER_DIED` (0x40000000) 비트를 세우고 대기자 하나를 깨운다.
3. 다음에 잡는 glibc 코드가 OWNER_DIED 를 보면 락을 가져가면서 `EOWNERDEAD` 를 돌려준다.

strace 로 본 1번 (practice 실행, 실제 출력 일부): 모든 프로세스·스레드가 시작할 때 등록한다. fork 한 자식도 다시 등록하는 것이 보인다.

```text
set_robust_list(0xebaedfce6040, 24)     = 0
[pid 262658] set_robust_list(0xebaedfadd240, 24) = 0
[pid 262661] set_robust_list(0xebaedfce6040, 24) = 0
[pid 262661] +++ killed by SIGKILL +++
--- SIGCHLD {si_signo=SIGCHLD, si_code=CLD_KILLED, si_pid=262661, si_uid=501, si_status=SIGKILL, si_utime=0, si_stime=0} ---
```

2·3번을 락 단어로 확인한 데모 (`demo_owner_death.c`, glibc 내부 필드 `__data.__lock`, `__data.__owner` 를 관찰용으로만 읽음, 실제 출력):

```text
== 한 번씩 자세히 ==
  [normal] 자식 tid=259226 가 쥔 상태: __lock=0x00000001 __owner=259226
  [normal] timedlock → Connection timed out, 사망 후 890.229 ms 만에 반환. 반환 직후 __lock=0x00000002 __owner=259226 (내 tid=259225)
  [robust] 자식 tid=259266 가 쥔 상태: __lock=0x0003f4c2 __owner=259266
  [robust] timedlock → EOWNERDEAD, 사망 후 0.084 ms 만에 반환. 반환 직후 __lock=0x8003f499 __owner=2147483647 (내 tid=259225)
  [robust, 대기자 없음] 죽은 tid=259267, 죽은 뒤 __lock=0x40000000 (FUTEX_OWNER_DIED=0x40000000, TID 부분=0)
  [robust, 대기자 없음] 다음 lock → EOWNERDEAD, __lock=0x0003f499
== robust 21 회: EOWNERDEAD 21 회, 사망→깨어남 min 0.046 / median 0.068 / max 0.137 ms ==
```

읽는 법:

- 일반 뮤텍스의 단어는 OS-A01 의 0/1/2 그대로다 (1 = 잠김, 대기자가 오자 2). 주인 TID 는 별도 필드 `__owner` 에만 있고 커널은 그것을 모른다. 주인이 죽은 뒤에도 대기자는 마감(1000 ms, 사망 시점 기준으로는 890 ms)까지 그냥 잔다.
- robust 뮤텍스의 단어는 **주인 TID 자체**다 (0x3f4c2 = 259266). 커널이 "이 단어의 주인이 죽는 스레드인가" 를 단어만 보고 판단할 수 있는 이유다. 대기자가 없을 때 죽으면 단어는 정확히 `0x40000000` — TID 부분은 지워지고 OWNER_DIED 만 남는다.
- 이미 잠들어 있던 대기자는 사망 후 **0.05–0.14 ms** 만에 `EOWNERDEAD` 로 깨어났다 (21 회, VM load 0.08). 커널이 exit 경로에서 깨워 주기 때문이다. 이 숫자는 VM 의 vCPU 위에서 잰 것이라 비율로만 볼 것.
- 회복한 쪽의 `__owner` 는 `2147483647` (INT_MAX) — glibc 소스의 `PTHREAD_MUTEX_INCONSISTENT` 표시로 보인다 (값만 관찰, 소스는 이 레슨에서 열어 보지 않았다). `consistent` 가 이것을 내 TID 로 바꾼다. 대기하다 깬 경우 단어에 `0x80000000` (FUTEX_WAITERS) 비트가 남아 있었다 — 다른 대기자가 있을 수 있다고 보수적으로 가정한 흔적이다.

**락은 살아났는데 데이터는?** — 이 레슨의 진짜 핵심이다. `EOWNERDEAD` 는 "직전 주인이 임계 구역 **어디에서** 죽었는지 모른다" 는 뜻이다. `ring_put` 의 갱신은 `tail++`, `count++`, `sum_put += v` 세 단계라서, 중간에 죽으면 `count != tail - head` 인 찢어진 상태가 남는다. 이 레슨의 해법은 작은 **undo 저널**이다.

| 단계 | 코드 | 여기서 죽으면 → 복구 결과 |
|---|---|---|
| 락 직후 (`CR_LOCKED`) | — | 저널 없음, 불변식 OK → 아무것도 안 함 (`ring_repair` = 0) |
| 저널 시작 | `j = m; fence; j_active = 1; fence` | `j_active == 0` 이면 아직 m 을 안 건드림 → 0 |
| 데이터 칸 쓰기 | `buf[tail % CAP] = v` | 살아 있는 영역 밖이라 무해 |
| 갱신 도중 (`CR_MID`) | `tail++` 후 | `j_active == 1` → `m = j` 로 되돌림 (`ring_repair` = 1) |
| 커밋 | `count++; sum += v; fence; j_active = 0` | 커밋됨 → 값은 살아남음 |
| 커밋 후 (`CR_COMMITTED`) | signal·unlock 전 | 데이터는 일관, **signal 이 빠짐** → Part C |

`atomic_signal_fence(memory_order_seq_cst)` 는 **컴파일러** 장벽이다. 죽는 것은 같은 CPU 위의 이 프로세스이고, 커널의 exit 처리와 다른 프로세스의 lock(acquire)이 하드웨어 순서를 보장하니 하드웨어 장벽은 필요 없다. 하지만 컴파일러가 `j_active = 1` 을 `j = m` 복사보다 앞으로 옮기면, 그 사이에 죽었을 때 쓰레기 저널로 "되돌리게" 된다. 지금 테스트는 크래시 지점이 함수 호출(`raise`)이라 컴파일러가 마음대로 못 옮기지만, 임의 시점 kill(OS-A12 의 크래시 주입)에서는 이 장벽이 의미를 가진다.

B 실측 — 부모가 "tail 만 올리고 멈춘" 자식을 죽인 경우:

```text
PASS read(pfd[0], &ch, 1) == 1 && ch == 'k'
PASS killed_by(wait_child(p, 3000), SIGKILL)
PASS r->j_active == 1 && r->m.tail == before.tail + 1 && ring_check(r) > 0
PASS ring_lock(r, 500) == 0 && r->recoveries == 2 && r->rollbacks == 1
PASS memcmp(&r->m, &before, sizeof before) == 0 && ring_check(r) == 0
PASS ring_get(r, &v, 500) == 0 && v == 7 && ring_check(r) == 0
```

`consistent` 를 빼먹은 경우:

```text
PASS raw_timedlock(&r->mu, 500) == EOWNERDEAD
PASS pthread_mutex_unlock(&r->mu) == 0
  robust, no consistent: parent=State not recoverable  other process=State not recoverable
PASS e1 == ENOTRECOVERABLE && e2 == ENOTRECOVERABLE
PASS ring_lock(r, 100) == ENOTRECOVERABLE
PASS shring_destroy(r, nr) == 0
```

`EOWNERDEAD` 를 받고 "모르겠다" 하며 unlock 하는 것은 POSIX 가 의도한 탈출구이기도 하다: "이 데이터는 못 믿겠으니 아무도 쓰지 마라". 복구할 방법이 없는 자료구조라면 그것이 정직한 선택이다 — 대신 모두가 `ENOTRECOVERABLE` 를 처리할 줄 알아야 한다 (연습 2: destroy 후 같은 자리에 다시 init).

### Part C — 조건변수는 robust 가 아니다

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">CR_COMMITTED: 생산자가 커밋하고 signal 전에 죽음 — 소비자는 50 ms 조각으로 스스로 깨어난다</text>
  <text x="10" y="60" fill="currentColor">소비자</text>
  <text x="10" y="150" fill="currentColor">생산자</text>
  <path d="M80 55 L720 55" stroke="currentColor" stroke-dasharray="2 3"/>
  <path d="M80 145 L720 145" stroke="currentColor" stroke-dasharray="2 3"/>
  <rect x="80" y="40" width="300" height="30" class="f-mut" stroke="currentColor"/>
  <text x="90" y="60" fill="currentColor" font-family="monospace" font-size="12">cond_timedwait(not_empty, ≤50ms)</text>
  <rect x="150" y="130" width="150" height="30" class="f-ok" stroke="currentColor"/>
  <text x="158" y="150" fill="currentColor" font-family="monospace" font-size="12">lock · put · commit</text>
  <rect x="300" y="130" width="60" height="30" class="f-warn" stroke="currentColor"/>
  <text x="308" y="150" fill="currentColor" font-family="monospace" font-size="12">KILL</text>
  <text x="300" y="185" fill="currentColor" font-size="12">signal·unlock 없음 → 단어 = OWNER_DIED</text>
  <path d="M380 70 L380 100" stroke="currentColor"/>
  <text x="300" y="115" fill="currentColor" font-size="12">조각 시간 초과 → 뮤텍스 재획득 시도</text>
  <rect x="390" y="40" width="160" height="30" class="f-acc" stroke="currentColor"/>
  <text x="398" y="60" fill="currentColor" font-family="monospace" font-size="12">→ EOWNERDEAD</text>
  <rect x="550" y="40" width="170" height="30" class="f-ok" stroke="currentColor"/>
  <text x="558" y="60" fill="currentColor" font-family="monospace" font-size="12">repair·consistent</text>
  <text x="400" y="95" fill="currentColor" font-size="12">while (count == 0) 재확인 → 4242 수거</text>
  <text x="10" y="225" fill="currentColor">무한 cond_wait 였다면: 아무도 signal 하지 않으니 영원히 잔다 (값은 링 안에 있는데도).</text>
  <text x="10" y="247" fill="currentColor">CR_MID 였다면: 되돌려져 값이 없다 → 조건 재확인 후 다시 잔다 → 전체 마감에 ETIMEDOUT.</text>
</svg>
```

`pthread_cond_timedwait` 은 돌아오기 전에 뮤텍스를 다시 잡으므로, POSIX 는 robust 뮤텍스와 함께 쓰일 때 이 함수도 `EOWNERDEAD` 를 돌려줄 수 있다고 명시한다 (락은 쥔 상태). 그래서 `ring_wait` 의 `EOWNERDEAD` 처리는 `ring_lock` 과 같다. 다른 점은 "언제 알게 되나" 다: 대기자는 **뮤텍스가 아니라 조건변수의 futex 에서** 자고 있으므로, 주인이 죽을 때 커널이 깨우는 대상(뮤텍스 futex 의 대기자)에 들지 않는다. 죽은 생산자가 signal 을 못 보냈으니 깨워 줄 사람도 없다. 해결은 두 가지를 같이 쓰는 것이다.

1. **조각 대기**: 긴 대기를 50 ms 조각의 `timedwait` 로. 조각 초과 = 가짜 깨어남 취급 → 조건 재확인. 대가: 할 일 없는 소비자가 초당 20 번 깨어난다 (임베디드 전력 예산에서 따져 볼 것).
2. **회복자의 broadcast**: `recover_locked` 가 두 조건변수를 broadcast 한다. 죽은 주인이 보냈어야 할 signal 을 대신 보내는 것이다. signal 이 아니라 broadcast 인 이유: 누가 무엇을 기다리는지 모르니 모두 깨워서 각자 조건을 다시 보게 한다 (가짜 깨어남은 원래 허용).

C 실측:

```text
== Part C: 조건변수 대기 중 주인 사망, 생산자 재시작 ==
PASS ok
PASS killed_by(wait_child(prod, 3000), SIGKILL)
  CR_COMMITTED: consumer exit=0 recoveries 0→1 tail 0→1
PASS exited_ok(sc) && r->recoveries == rec0 + 1 && ring_check(r) == 0
PASS r->rec_in_wait == w0 + 1
PASS r->m.tail == tail0 + (mode == CR_COMMITTED ? 1u : 0u)
  CR_MID: consumer exit=0 recoveries 1→2 tail 1→1
  supervisor: restarts=3 consumer exit=0 recoveries=3 rollbacks=1 tail=3000 sum ok=1
PASS restarts == 3 && exited_ok(sc)
PASS q->recoveries == 3 && q->rollbacks == 1
PASS count_leftover_shm() == 0
47/47 passed
```

`rec_in_wait == w0 + 1` 이 "회복이 소비자의 `cond_timedwait` 안에서 일어났다" 의 증거다 (그 시간 동안 뮤텍스를 만지는 다른 프로세스가 없도록 테스트를 짰다). 감시자 시나리오에서 복구가 정확히 3 번인 이유: 죽음 한 번마다 단어에 OWNER_DIED 가 한 번 서고, `EOWNERDEAD` 는 다음 locker **한 명**에게만 간다. 그게 재시작된 생산자의 첫 `ring_lock` 일 수도, 소비자의 조각 대기일 수도 있지만 횟수는 항상 3 이다. 되돌림이 1 번인 이유: 갱신 도중(`CR_MID`) 죽음만 저널이 열려 있다.

**조건변수의 더 나쁜 함정 — 대기 중에 죽은 프로세스** (`demo_cond_death.c`, 실제 출력):

```text
== ① 죽은 대기자 k 명 뒤의 signal/broadcast 한 번 → 살아 있는 대기자 B (10 회씩) ==
  죽은 대기자 0, signal    → B 깨어남 10/10
  죽은 대기자 0, broadcast → B 깨어남 10/10
  죽은 대기자 1, signal    → B 깨어남 10/10
  죽은 대기자 1, broadcast → B 깨어남 10/10
  죽은 대기자 2, signal    → B 깨어남 10/10
  죽은 대기자 2, broadcast → B 깨어남 10/10
== ② 죽은 대기자가 있던 조건변수에 pthread_cond_destroy ==
  죽은 대기자 0 명            : destroy → 0 (정상)
  죽은 대기자 1 명, 깨움 없음: destroy → 멈춤 → alarm(2) 으로 죽음
  죽은 대기자 1 명 + signal 1 번: destroy → 멈춤 → alarm(2) 으로 죽음
  죽은 대기자 1 명 + broadcast 1 번: destroy → 멈춤 → alarm(2) 으로 죽음
```

정직하게 적는다: 처음에는 "죽은 대기자가 signal 을 삼켜서 살아 있는 대기자가 못 깨어난다" 를 예상했는데 이 순서(죽은 대기자가 먼저, 살아 있는 B 가 나중, signal 한 번)에서는 **재현되지 않았다** (죽은 대기자가 있는 40 회 모두 깨어남). 대신 glibc 2.43 의 `pthread_cond_destroy` 는 죽은 대기자가 한 명이라도 있었으면 **영원히 멈췄다** — signal 이나 broadcast 로 깨운 뒤에도. glibc 의 조건변수는 대기자가 "나 나갔다" 를 확인해 줄 때까지 destroy 가 기다리도록 짜여 있고(glibc 소스 주석에 따르면 대기자 참조 수를 센다 — 이 레슨에서 소스를 열어 확인하지는 않았다), 죽은 대기자는 그 확인을 영원히 못 한다. POSIX 는 "대기 중에 죽은 프로세스" 에 대해 아무것도 약속하지 않으니 다른 libc 는 다르게 굴 수 있다.

실무 결론: (1) 조건변수에는 robust 속성이 **없다** (`pthread_condattr_setrobust` 같은 것은 POSIX 에 없다). (2) 대기자가 죽을 수 있는 조건변수는 destroy 하지 말고 **shm 객체째 버린다** (`munmap` + `shm_unlink` 는 멈추지 않는다). 그래서 `shring_destroy` 주석에 "모든 대기자가 살아서 나간 뒤에만" 이라고 적었다. (3) 대기는 항상 timed + 조건 재확인. (4) 정말로 견고해야 하면 조건변수 대신 생산자·소비자 하트비트 카운터와 시퀀스 번호로 상대의 죽음을 **감지**하는 프로토콜이 필요하다 — 그것이 OS-A12 (견고한 공유 메모리 IPC)의 주제다. 여기서는 언급만 한다.

### 맥 대조와 TSan 메모

맥에서 같은 속성을 써 보면 (실제 출력, `MacOSX26.4.sdk`):

```text
mac_robust.c:7:12: error: call to undeclared function 'pthread_mutexattr_setrobust'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
mac_robust.c:7:44: error: use of undeclared identifier 'PTHREAD_MUTEX_ROBUST'
```

SDK 의 `pthread.h` 에 robust 관련 선언이 하나도 없고(`grep robust` 0 건), `man pthread_mutexattr_setrobust` 도 없다. 반면 `sys/errno.h` 에 `EOWNERDEAD 105`, `ENOTRECOVERABLE 104` 는 정의돼 있고 `PTHREAD_PROCESS_SHARED` · `pthread_mutexattr_setpshared` 선언도 있다 (pshared 뮤텍스가 맥에서 프로세스 간에 실제로 동작하는지는 이 레슨에서 돌려 보지 않았다). 맥에서 같은 문제를 풀려면 연습 4 의 "주인 pid + 생존 확인" 같은 대용품이나, 프로세스가 죽으면 커널이 자동으로 풀어 주는 자원 — `flock`/`fcntl` 파일 락(프로세스 종료 시 해제), 소켓 연결 끊김 — 을 쓴다.

TSan: solution 은 스레드를 안 쓰는 다중 **프로세스** 프로그램이라 TSan 이 볼 것이 없다 (프로세스 간 동기화는 TSan 모델 밖). 그래도 clang 21 TSan 으로 돌려 보면 `unlock of an unlocked mutex` 경고가 났다. 원인을 작은 탐침(스레드가 robust 뮤텍스를 쥔 채 종료 → main 이 다시 잡기)으로 좁혀 보니 `pthread_mutex_lock` 이 `EOWNERDEAD` 를 돌려준 경우는 경고 0 건, `pthread_mutex_timedlock` 이 `EOWNERDEAD` 를 돌려준 경우는 3 회 중 3 회 경고였다 — TSan 의 `timedlock` 가로채기가 `EOWNERDEAD` 를 "락 획득" 으로 기록하지 않는 오탐이다. ASan+UBSan 은 gcc·clang 모두 깨끗하다 (6. 정답 코드).

### 펌웨어 연결

- **RTOS**: FreeRTOS 에는 "프로세스" 도 주소 공간 분리도 없어서 이 문제가 다른 모양으로 나온다. 뮤텍스를 쥔 태스크를 `vTaskDelete` 하면 그 뮤텍스는 풀리지 않는다 (RTOS-B10 의 소유권 개념). 보통의 대응은 "태스크를 죽이지 말고 시스템 리셋" 이다. MMU 가 있는 리눅스에서는 프로세스 하나만 죽고 재시작할 수 있으니, 공유 상태를 그 재시작에서 살아남게 만드는 것이 엔지니어의 몫이 된다.
- **우선순위 상속과 같이**: 실시간 데몬이면 `pthread_mutexattr_setprotocol(PTHREAD_PRIO_INHERIT)` 와 robust 를 같이 쓸 수 있다. glibc 는 이 조합을 `FUTEX_LOCK_PI` 경로로 처리한다 (OS-A01 의 PI 설명, RTOS-M04 의 상속 구현과 연결). 이 레슨에서는 실행해 보지 않았다.
- **execve 도 죽음이다**: 리눅스 man 페이지는 pshared robust 뮤텍스의 주인이 unlock 없이 `execve` 하면 대기자가 `EOWNERDEAD` 를 받는다고 적는다 (POSIX 는 이 부분을 정하지 않음). 락을 쥔 채 `system()`/`exec` 하는 데몬 코드를 리뷰할 때 기억할 것.
- **설계 원칙**: shm 안에는 포인터 대신 인덱스, 갱신은 저널 또는 "데이터 먼저, 공개는 한 단어 원자 쓰기" (DS-A01 의 SPSC 큐가 락 없이 그렇게 한다), 대기는 항상 timed, 상대 생존은 따로 감지 (OS-A12).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

검증 (VM `/tmp/OS-M17/`, 실제 결과):

- gcc 15.2 strict (`-std=c11 -Wall -Wextra -Werror -O2 -pthread`): **47/47**, 5 회 반복 모두 47/47 (각 0.81–0.83 s — 그중 약 0.7 s 는 일반 뮤텍스 `ETIMEDOUT` 150 ms ×2, `CR_MID` 소비자의 400 ms 마감, 50 ms 조각 대기 같은 의도된 대기). 다른 cwd(`cd / && /tmp/OS-M17/so`)에서도 47/47.
- clang 21 strict: 47/47. 시작 코드: gcc·clang 모두 경고 0, 1/5, exit 1.
- `-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer`: gcc·clang 모두 solution 47/47, practice 26/26, 오류 0.
- clang `-fsanitize=thread`: 위 "TSan 메모" 의 오탐 때문에 경고가 나고, 경고가 난 자식은 TSan 규칙대로 exit 66 으로 끝나 일부 CHECK 가 FAIL (solution 44/47, practice 24/26). 동작 문제는 아니다.
- 실행 후 `ls /dev/shm | grep -c OS-M17` → `0`.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 공유 메모리에 두기만 하고 `PTHREAD_PROCESS_SHARED` 를 안 줌 | 기본은 private futex → 다른 프로세스의 대기자와 키가 달라 못 깨움 (OS-A01 실측). POSIX 로도 정의되지 않은 동작 | `pthread_mutexattr_setpshared(&a, PTHREAD_PROCESS_SHARED)`, 조건변수도 |
| 여는 쪽도 `pthread_mutex_init` 호출 | 남이 쥔 락을 덮어씀 → 상호 배제 붕괴 | 만드는 쪽만 `O_CREAT\|O_EXCL` 후 init, 여는 쪽은 `O_RDWR` + `magic` 확인 |
| `ftruncate` 생략 | 크기 0 객체를 mmap → 접근 시 SIGBUS | `ftruncate(fd, sizeof(shring_t))` |
| `EOWNERDEAD` 를 실패로 보고 바로 return | 락을 쥔 채 return → 이번엔 **살아 있는** 내가 영원히 쥠 | `EOWNERDEAD` = 획득. repair → consistent → 계속 |
| repair 없이 `consistent` 만 | 락은 살아나도 데이터가 찢어진 채 (`count != tail - head`) → 나중에 이상한 값 | 저널로 되돌리거나 불변식 재계산 후 consistent |
| `consistent` 없이 unlock | 영구 `ENOTRECOVERABLE`, 모든 프로세스 | 고칠 수 있으면 consistent, 못 고치면 의도적으로 그렇게 하고 모두가 처리 |
| `pthread_cond_wait` 의 반환값 무시 | robust 면 `EOWNERDEAD` 가 올 수 있다 → 불일치 상태로 진행 | `ring_wait` 처럼 `EOWNERDEAD` → 회복 |
| 무한 `pthread_cond_wait` | 죽은 생산자의 signal 은 영원히 안 온다 | timed 조각 대기 + 조건 재확인, 회복자는 broadcast |
| shm 안에 포인터 저장 | 프로세스마다 매핑 주소가 다르다 | 인덱스·오프셋 |
| 이름을 `shm_unlink` 안 함 | `/dev/shm` 에 영구히 남음 (재부팅까지), 다음 실행의 `O_EXCL` 이 `EEXIST` | 고유 이름(`/<ID>_<pid>_tag`) + 정리 + 테스트에서 잔여물 검사 |
| 죽은 대기자가 있던 조건변수를 `pthread_cond_destroy` | glibc 2.43 에서 영원히 멈춤 (실측) | 대기자 사망 가능성이 있으면 shm 째 `munmap`/`shm_unlink` |
| `timedlock` 마감을 `CLOCK_MONOTONIC` 으로 계산 | POSIX `timedlock` 은 `CLOCK_REALTIME` → 마감이 엉뚱함 | `deadline_on(CLOCK_REALTIME, ms)` 또는 glibc `pthread_mutex_clocklock` |

## 8. 면접 Q&A

<details>
<summary>Q1. 공유 메모리의 뮤텍스를 쥔 프로세스가 크래시하면? 꼬리: 일반 뮤텍스를 어떻게든 풀 수는 없나?</summary>

일반(STALLED) 뮤텍스는 그대로 잠긴다. futex 단어는 그냥 int 이고 커널은 그것이 누구 것인지 모른다. 이 레슨 실측: 부모와 제3의 프로세스 모두 `ETIMEDOUT`. 꼬리: 다른 프로세스가 unlock 하는 것은 POSIX 상 정의되지 않은 동작이고(주인만 unlock), 단어를 강제로 0 으로 쓰는 해킹은 잠들어 있는 대기자를 못 깨우고 데이터 불변식도 모른다. 정상적인 방법은 shm 객체를 버리고 모두 재시작하는 것, 또는 처음부터 robust 로 만드는 것.

</details>

<details>
<summary>Q2. robust mutex 는 커널이 어떻게 지원하나? 꼬리: 락을 잡는 도중(목록에 넣기 전)에 죽으면?</summary>

glibc 가 스레드마다 "쥐고 있는 robust 뮤텍스" 의 연결 리스트를 사용자 공간에 두고 그 머리 주소를 `set_robust_list` 로 등록한다 (strace 로 모든 프로세스·스레드 시작 시 1 번씩 확인). 잡기·풀기 때 리스트 조작은 syscall 없이 사용자 공간에서 한다. 스레드가 끝나면 커널이 그 리스트를 걸어 다니며 단어의 TID 가 죽는 스레드인 항목에 `FUTEX_OWNER_DIED` 를 세우고 대기자 하나를 깨운다. 단어에 TID 가 들어 있어야 하는 이유가 이것이다 (실측 단어 = 0x3f4c2 = TID). 꼬리: 리스트 머리에 `list_op_pending` 칸이 있어서 "지금 이 락을 넣거나 빼는 중" 을 적어 두고, 커널은 그것도 확인한다 (커널 문서 robust-futex-ABI 기준).

</details>

<details>
<summary>Q3. EOWNERDEAD 를 받았을 때 무엇을 해야 하나? 순서는?</summary>

이미 락은 내 것이다. ① 공유 데이터의 불변식을 확인·복구 (이 레슨은 undo 저널로 반쯤 한 갱신을 되돌림) ② 죽은 주인이 못 보냈을 수 있는 조건변수 깨움을 broadcast 로 대신 ③ `pthread_mutex_consistent` ④ 평소처럼 진행·unlock. 복구 도중 나도 죽으면 다음 사람이 또 `EOWNERDEAD` 를 받으므로 복구 코드 자체도 반복 실행에 안전해야(멱등) 한다 — `m = j` 되돌리기는 여러 번 해도 같다. 고칠 수 없으면 consistent 없이 unlock → `ENOTRECOVERABLE` 로 모두에게 "이 데이터는 죽었다" 를 알린다.

</details>

<details>
<summary>Q4. 왜 pthread_cond_wait 도 EOWNERDEAD 를 돌려주나? 꼬리: 그러면 조건변수로 기다리던 소비자는 주인이 죽은 순간 깨어나나?</summary>

`cond_wait` 은 돌아오기 전에 뮤텍스를 다시 잡기 때문이다. 그 재획득이 robust 뮤텍스의 lock 이니 결과도 같다 (POSIX `pthread_cond_timedwait` 의 오류 목록). 꼬리: 아니다. 소비자는 조건변수 futex 에서 자고 있어서 커널이 깨우는 뮤텍스 대기자 목록에 없다. 누군가 signal 하거나 자기 타임아웃이 와야 깬다. 그래서 timed 조각 대기가 필요하다. 이 레슨에서 소비자는 50 ms 조각이 끝날 때 `EOWNERDEAD` 를 받았고 (`rec_in_wait` +1) 커밋된 값 4242 를 가져갔다.

</details>

<details>
<summary>Q5. 조건변수를 robust 하게 만들 수는 없나? 꼬리: 대기자가 죽으면 무슨 문제가 생기나?</summary>

POSIX 에는 조건변수용 robust 속성이 없다. 조건변수에는 "주인" 이 없어서 "주인이 죽었다" 를 정의할 수 없다. 대기자가 죽는 쪽의 문제는 구현마다 다르다. glibc 2.43 실측: 대기 중 SIGKILL 된 프로세스가 한 명이라도 있으면 `pthread_cond_destroy` 가 signal/broadcast 를 보낸 뒤에도 영원히 멈췄다. 살아 있는 다른 대기자에 대한 signal 은 이 실험 순서에서는 잃어버리지 않았다 (40/40 깨어남). 대응: 대기자가 죽을 수 있는 조건변수는 destroy 하지 않고 shm 째 버린다. 견고성이 필요하면 하트비트·시퀀스 번호로 상대를 감지하는 프로토콜(OS-A12)로 간다.

</details>

<details>
<summary>Q6. 반쯤 고친 상태로 죽은 데이터는 어떻게 복구하나? 꼬리: 왜 signal fence 를 썼나, 하드웨어 장벽은?</summary>

갱신을 시작하기 전에 바뀔 메타데이터를 저널에 복사하고 `j_active = 1`, 다 바꾼 뒤 `j_active = 0` (커밋). 회복자는 `j_active` 면 저널로 되돌린다 — DB 의 undo log, 파일시스템 저널(OSTEP 42장)과 같은 생각이다. 데이터 칸(`buf[tail]`)은 살아 있는 영역 밖이라 되돌릴 필요가 없다. 꼬리: 죽는 것은 이 프로세스 자신이므로 문제는 **컴파일러**가 `j_active = 1` 을 저널 복사보다 앞으로 옮기는 것뿐이다. 하드웨어 순서는 커널의 exit 처리와 회복자의 lock(acquire)이 맞춰 준다. 다른 프로세스가 락 없이 이 값을 읽는 설계였다면 release/acquire 원자 연산이 필요했을 것이다 (DS-A01).

</details>

<details>
<summary>Q7. macOS(또는 robust 가 없는 RTOS)에서는 어떻게 하나? 꼬리: kill(pid, 0) 로 생존 확인할 때의 함정은?</summary>

macOS SDK 에는 `pthread_mutexattr_setrobust` 자체가 없다 (컴파일 오류 실측). 대안: ① 프로세스가 죽으면 커널이 자동으로 풀어 주는 자원을 락으로 쓴다 — `flock`/`fcntl` 레코드 락, Unix 소켓 연결 ② 락 단어에 주인 pid 를 쓰고 `kill(pid, 0)` 이 `ESRCH` 면 빼앗는다 (연습 4). 꼬리: 죽었지만 아직 `wait` 안 된 **좀비**에게도 `kill(pid, 0)` 은 성공한다 (연습 4 실측: 수거 전 `ETIMEDOUT`, 수거 후 빼앗음). 그리고 pid 는 재사용되므로 오래 지나면 엉뚱한 프로세스를 살아 있다고 볼 수 있다 — 리눅스면 pidfd (OS-M18)가 이 경쟁을 없앤다. 빼앗은 뒤의 데이터 복구 문제는 robust 와 똑같이 남는다.

</details>

<details>
<summary>Q8. 스레드만 죽는 경우에도 EOWNERDEAD 가 오나? 꼬리: execve 는?</summary>

온다. robust list 는 스레드 단위라서, 락을 쥔 스레드가 `pthread_exit` 하면 같은 프로세스의 다른 스레드가 `EOWNERDEAD` 를 받는다 (연습 1 실측. 같은 상황의 일반 뮤텍스는 `ETIMEDOUT`). 꼬리: 리눅스 man 페이지에 따르면 pshared robust 뮤텍스의 주인이 unlock 없이 `execve` 해도 대기자가 `EOWNERDEAD` 를 받는다 — exec 는 옛 주소 공간과 스레드를 끝내기 때문이다. POSIX 는 이 부분을 정하지 않았다고 man 페이지가 적는다. 이 레슨에서 exec 경우는 실행해 보지 않았다.

</details>

## 9. 추가 연습

### 연습 1 — 스레드가 쥔 채 종료

같은 프로세스 안에서 스레드가 (private) robust 뮤텍스를 잡고 unlock 없이 `pthread_exit`. main 의 `timedlock` 결과는? 일반 뮤텍스로도 해 보라.

<details>
<summary>답</summary>

robust → `EOWNERDEAD` (consistent 후 unlock), 일반 → `ETIMEDOUT`. robust list 는 프로세스가 아니라 **스레드** 단위로 등록되고(strace 에서 스레드마다 `set_robust_list`), 스레드 종료 때도 커널이 목록을 걷는다. pshared 가 아니어도 된다.

```c
static void *lock_and_exit(void *arg) { pthread_mutex_lock(arg); pthread_exit(NULL); }
...
pthread_create(&t, NULL, lock_and_exit, &m); pthread_join(t, NULL);
int e = timedlock_ms(&m, 100);              /* robust: EOWNERDEAD, normal: ETIMEDOUT */
```

</details>

### 연습 2 — ENOTRECOVERABLE 뒤에 다시 쓰기

`ENOTRECOVERABLE` 이 된 pshared robust 뮤텍스를 같은 주소에서 다시 쓸 수 있게 하라. 덤: 일관된 뮤텍스에 `pthread_mutex_consistent` 를 부르면? robust 뮤텍스를 주인 아닌 스레드가 unlock 하면?

<details>
<summary>답</summary>

허용되는 연산은 `pthread_mutex_destroy` 뿐 → destroy 후 같은 자리에서 `pthread_mutex_init` (pshared + robust). 다른 프로세스는 같은 주소를 계속 쓰므로 바로 잡을 수 있다 (자식 프로세스로 확인). **단 재init 하는 동안 아무도 그 뮤텍스를 쓰지 않아야 한다** — 보통은 "ENOTRECOVERABLE 이면 모두 멈추고 관리자 프로세스 하나가 재생성" 같은 프로토콜이 필요하다.

```c
static int reinit_after_notrecoverable(pthread_mutex_t *m) {
    int e = pthread_mutex_destroy(m);
    if (e) return e;
    return init_mutex(m, 1, 1);
}
```

실측: 일관된 뮤텍스에 consistent → `EINVAL`, 주인 아닌 스레드의 unlock → `EPERM` (glibc 의 robust 뮤텍스는 unlock 때 주인을 검사한다 — 단어에 주인 TID 가 있으니 싸게 할 수 있다).

</details>

### 연습 3 — 두 개를 쥔 채 죽기, 이미 기다리던 프로세스

자식이 robust 뮤텍스 A, B 를 모두 잡고 멈춘다. 다른 프로세스 W 가 A 에서 3 초 `timedlock` 으로 기다리는 중에 부모가 자식을 SIGKILL. W 와 부모는 각각 무엇을 받나?

<details>
<summary>답</summary>

W 는 A 에서 `EOWNERDEAD` 로 깨어나고 (consistent 후 unlock), 그 뒤 부모의 A lock 은 평범한 0. 부모의 B lock → `EOWNERDEAD`. 커널은 목록 전체를 걷으므로 쥐고 있던 **모든** robust 뮤텍스가 표시된다. W 가 잠들었는지 아닌지와 무관하게 결과가 같아서 결정적 테스트가 된다 (잠들었으면 커널이 깨우고, 아직이면 lock 이 OWNER_DIED 를 본다). 실제로 잠든 대기자가 얼마나 빨리 깨는지는 `demo_owner_death.c` 의 0.05–0.14 ms.

</details>

### 연습 4 — robust 가 없는 OS 의 대용품: pid 락

`atomic_int owner` (0 = 풀림, 아니면 주인 pid) 하나로 프로세스 간 락을 만들어라. CAS 0→내 pid 로 잡고, 실패하면 `kill(owner, 0)` 이 `ESRCH` 일 때 CAS owner→내 pid 로 **빼앗고** `EOWNERDEAD` 를 돌려준다. 주인이 SIGKILL 된 뒤 부모가 `waitpid` 하기 전과 후의 결과를 비교하라.

<details>
<summary>답</summary>

```c
static int pid_gone(int pid) { return kill(pid, 0) == -1 && errno == ESRCH; }
static int pidlock_lock(pidlock_t *l, int timeout_ms) {
    int me = (int)getpid();
    for (int t = 0; t <= timeout_ms; t++) {
        int cur = 0;
        if (atomic_compare_exchange_strong(&l->owner, &cur, me)) return 0;
        if (pid_gone(cur) && atomic_compare_exchange_strong(&l->owner, &cur, me)) return EOWNERDEAD;
        sleep_ms(1);
    }
    return ETIMEDOUT;
}
```

실측: 수거 전 `ETIMEDOUT` (좀비는 아직 pid 를 차지 → `kill(pid,0)` 성공), `waitpid` 후 `EOWNERDEAD` 로 빼앗음. 빼앗기 CAS 가 `cur` 를 기대값으로 쓰므로 두 프로세스가 동시에 빼앗으려 해도 한 명만 성공한다. 한계: 폴링(잠들기 대신 1 ms sleep), pid 재사용, 주인의 부모가 수거하지 않으면 영원히 좀비 — 그래서 커널이 직접 해 주는 robust futex 가 낫다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
# VM 안 /tmp/OS-M17/
gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr      # 26/26 passed, 0.20 s
gcc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread practice.c -o pr_as && ./pr_as   # 26/26, 오류 0 (clang 도)
```

데모 (둘 다 strict 플래그로 gcc·clang 경고 0, 출력은 5. 풀이에 붙였다):

```bash
gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_owner_death.c -o od && ./od   # 약 1.2 s
gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_cond_death.c -o cdd && ./cdd # 약 6 s (alarm(2) 3 번)
strace -f -e trace=set_robust_list ./pr 2>&1 | grep set_robust | head
```

<!-- code: demo_owner_death.c -->

<!-- code: demo_cond_death.c -->

## 10. 핵심 정리

- 프로세스 간 락 = `shm_open` + `ftruncate` + `mmap(MAP_SHARED)` 위의 `PTHREAD_PROCESS_SHARED` 뮤텍스·조건변수. init 은 만드는 쪽 한 번만 (`O_EXCL`, magic 은 마지막), 이름은 `shm_unlink` 까지 남는다.
- 일반 뮤텍스의 주인이 죽으면 **영원히 잠긴다** (커널은 단어의 뜻을 모름). 테스트·운영 코드 모두 `timedlock` 으로 감지.
- robust: 단어 = 주인 TID, 스레드마다 `set_robust_list`, 죽으면 커널이 `OWNER_DIED` (0x40000000) 를 세우고 대기자를 깨움 (실측 0.05–0.14 ms) → 다음 한 명이 `EOWNERDEAD`.
- `EOWNERDEAD` = 락 획득 + "데이터를 고쳐라". repair → broadcast → `pthread_mutex_consistent`. 빼먹고 unlock → 영구 `ENOTRECOVERABLE` (destroy 만 허용).
- 락보다 어려운 것은 데이터: undo 저널(또는 한 단어 원자 공개)로 찢어진 갱신을 되돌린다. 컴파일러 재배치는 `atomic_signal_fence` 로 막는다.
- `pthread_cond_timedwait` 도 `EOWNERDEAD` 를 돌려준다. 죽은 주인의 signal 은 안 오니 timed 조각 대기 + 조건 재확인. 조건변수는 robust 가 아니다 — 죽은 대기자가 있으면 glibc `cond_destroy` 는 멈춘다(실측).
- 재시작한 프로세스는 shm 의 커밋된 상태(`tail`)에서 이어 쓴다. 상대 감지·버전·백프레셔는 OS-A12.
- macOS 에는 robust mutex 가 없다 → 파일 락·소켓·pid+생존 확인(좀비·pid 재사용 함정) 같은 대용품.

## 참고자료

- POSIX.1-2024: `pthread_mutexattr_setrobust`, `pthread_mutex_consistent`, `pthread_mutex_lock`/`timedlock` (EOWNERDEAD, ENOTRECOVERABLE), `pthread_cond_timedwait`, `pthread_mutexattr_setpshared`, `shm_open` — https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_mutexattr_setrobust.html 등
- Linux man-pages: `pthread_mutexattr_setrobust(3)` (VM 에서 읽음: STALLED/ROBUST 설명, execve 노트), `pthread_mutex_consistent(3)`, `get_robust_list(2)`/`set_robust_list(2)`, `shm_open(3)`, `shm_overview(7)`, `futex(2)` — https://man7.org/linux/man-pages/man3/pthread_mutexattr_setrobust.3.html
- Linux 커널 문서 `Documentation/locking/robust-futexes.rst`, `Documentation/locking/robust-futex-ABI.rst` (robust list, `list_op_pending`, `FUTEX_OWNER_DIED`)
- glibc 소스 `nptl/pthread_mutex_lock.c` (robust 경로), `nptl/pthread_cond_destroy.c` (대기자 확인 대기) — 이 레슨은 2.43 바이너리의 동작만 관찰했다
- Ulrich Drepper, *Futexes Are Tricky* (OS-A01), OSTEP 28장 Locks · 30장 Condition Variables · 42장 Crash Consistency (저널링 아이디어)
- 이 저장소: OS-B05 (뮤텍스), OS-B06 (조건변수 bounded buffer), OS-B14 (shm+세마포어 IPC 비교), OS-A01 (futex, PRIVATE vs shared 실측), OS-A12 (견고한 shm 프로토콜), OS-M18 (pidfd), DS-A01 (락 없는 SPSC 큐), RTOS-B10 (뮤텍스 소유권), RTOS-M04 (우선순위 상속)
