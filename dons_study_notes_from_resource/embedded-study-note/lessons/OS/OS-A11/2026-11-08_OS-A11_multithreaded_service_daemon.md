---
id: OS-A11
time: 120분
updated: 2026-11-08
---
# OS-A11 종합: 멀티스레드 서비스 데몬

## 학습 목표

- kqueue 이벤트 루프 + bounded queue + condvar 워커 풀로 된 서버에서 **"어떤 데이터를 어떤 스레드가 소유하는가"** 를 먼저 정하고, 락이 필요한 곳과 필요 없는 곳을 설명한다.
- 슬랩 할당기(size class, free list, 페이지 상한, ASan 독칠)를 직접 짜고, 왜 custom allocator 가 ASan 을 눈멀게 하는지 설명한다.
- SIGTERM **graceful drain** 을 "접수 / 거절 / 미응답" 세 상태로 정의하고, 응답 개수를 하나도 빠짐없이 세는 테스트로 증명한다. 일부러 만든 버그(큐 버리기)를 그 테스트가 잡는 것까지 보인다.
- SIGHUP **원자적 설정 교체**를 포인터 교환 + hazard slot 회수로 구현하고, "쥐고 있는 리더가 있는데 바로 해제" 버그의 ASan 보고서를 읽는다.
- append-only 로그 + CRC 로 **SIGKILL 뒤 복구**를 구현하고, `write` / `fsync` / `F_FULLFSYNC` 가 각각 무엇을 보장하는지 실측 숫자와 함께 말한다.

## 1. 문제

게이트웨이 ECU, BMC, 차량용 로거, 검증 장비의 상주 프로세스는 전부 "신호 받으면 얌전히 죽고, 설정은 재시작 없이 바꾸고, 전원이 나가도 기록을 잃지 않는" 데몬이다 — 시스템/펌웨어 면접의 마지막 질문이 대개 "이걸 하나로 합쳐 보세요" 이고, 8층까지 배운 동시성 도구가 **동시에** 필요한 첫 레슨이기도 하다. 따로따로 맞던 부품이 합쳐지면 부품 사이 경계(누가 깨우나, 누가 해제하나, 순서는 누가 보장하나)에서 깨진다.

만들 것: Unix domain socket 위의 줄 단위 KV 서버.

| 요청 | 응답 |
|---|---|
| `SET k v` | `OK` / `ERR nomem` / `ERR toolarge` / `ERR io` |
| `GET k` | `VAL v` / `NF` |
| `DEL k` | `OK` / `NF` |
| `CFG` | `CFG <version> <alpha> <beta> <name>` |
| (드레인 중 아무 요청) | `ERR draining` |
| `STATS` / `PAUSE` / `RESUME` | 테스트 훅, 이벤트 루프가 직접 답한다 |

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">구조: 한 스레드가 연결을, N 스레드가 요청을 소유한다</text>
  <rect x="10" y="40" width="130" height="70" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="20" y="62" fill="currentColor">클라이언트들</text>
  <text x="20" y="82" fill="currentColor">요청 1 개씩</text>
  <text x="20" y="100" fill="currentColor">(연결당 동시 1)</text>
  <rect x="190" y="40" width="190" height="140" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="200" y="62" fill="currentColor" font-weight="bold">이벤트 루프 (1 스레드)</text>
  <text x="200" y="84" fill="currentColor" font-family="monospace">kevent()</text>
  <text x="200" y="104" fill="currentColor">READ/WRITE/SIGNAL/USER</text>
  <text x="200" y="124" fill="currentColor">conn_t 소유 (락 없음)</text>
  <text x="200" y="144" fill="currentColor">inflight, limbo 소유</text>
  <text x="200" y="164" fill="currentColor">시그널은 EVFILT_SIGNAL</text>
  <path d="M140 75 L186 75" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M178 69 L188 75 L178 81" stroke="currentColor" fill="none" stroke-width="2"/>
  <rect x="430" y="40" width="140" height="56" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="440" y="62" fill="currentColor" font-weight="bold">bounded queue</text>
  <text x="440" y="82" fill="currentColor">try_push (안 막힘)</text>
  <path d="M380 68 L426 68" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M418 62 L428 68 L418 74" stroke="currentColor" fill="none" stroke-width="2"/>
  <rect x="610" y="40" width="120" height="100" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="620" y="62" fill="currentColor" font-weight="bold">워커 N</text>
  <text x="620" y="82" fill="currentColor">bq_pop(condvar)</text>
  <text x="620" y="102" fill="currentColor">cfg_acquire</text>
  <text x="620" y="122" fill="currentColor">kv + WAL</text>
  <path d="M570 68 L606 68" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M598 62 L608 68 L598 74" stroke="currentColor" fill="none" stroke-width="2"/>
  <rect x="430" y="130" width="140" height="56" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="440" y="152" fill="currentColor" font-weight="bold">완료 큐 + 깨우기</text>
  <text x="440" y="172" fill="currentColor">EVFILT_USER 트리거</text>
  <path d="M610 140 L574 150" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M430 150 L384 136" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M392 134 L382 135 L388 143" stroke="currentColor" fill="none" stroke-width="2"/>
  <rect x="10" y="220" width="200" height="74" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="20" y="242" fill="currentColor" font-weight="bold">슬랩 (5 클래스)</text>
  <text x="20" y="262" fill="currentColor">conn_t, req_t, kv node</text>
  <text x="20" y="282" fill="currentColor">페이지 상한 → ERR nomem</text>
  <rect x="240" y="220" width="220" height="74" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="250" y="242" fill="currentColor" font-weight="bold">KV 표 (64 줄무늬 락)</text>
  <text x="250" y="262" fill="currentColor">락 안에서 WAL 먼저, 표 나중</text>
  <text x="250" y="282" fill="currentColor">→ GET 은 기록된 값만 본다</text>
  <rect x="490" y="220" width="240" height="74" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="500" y="242" fill="currentColor" font-weight="bold">설정 (불변 객체)</text>
  <text x="500" y="262" fill="currentColor">게시 = exchange</text>
  <text x="500" y="282" fill="currentColor">회수 = hazard 슬롯 전부 비었을 때</text>
  <g fill="currentColor">
    <text x="10" y="326">규칙 1: 이벤트 루프는 절대 막히지 않는다 (try_push, 비차단 소켓, 짧은 락만).</text>
    <text x="10" y="348">규칙 2: req_t 소유권은 큐를 건널 때만 넘어간다. 워커는 conn_t 를 만지지 않는다.</text>
    <text x="10" y="370">규칙 3: 응답은 로그 기록 뒤에 나간다 → ack 받은 쓰기는 프로세스 충돌을 넘어 살아남는다.</text>
    <text x="10" y="392">규칙 4: 큐 상한(bounded)이 메모리 상한이다. 큐가 차면 그 연결의 파싱을 멈춘다(역압).</text>
  </g>
</svg>
```

요청 하나의 일생: 이벤트 루프가 소켓에서 읽어 줄을 자른다 → `req_t` 를 슬랩에서 받아 큐에 넣는다(`busy` 표시) → 워커가 꺼내 설정을 쥐고 실행(필요하면 로그)한다 → 완료 큐에 넣고 루프를 깨운다 → 루프가 응답을 복사하고 소켓이 받아 줄 만큼 쓴다.

**연결당 동시 요청 1 개** 로 정했다: 워커가 여러 개여도 같은 연결의 응답 순서가 자동으로 지켜진다 (순서 버퍼로 푸는 방법은 §9 연습 4).

`solution.c` 하나가 서버이자 테스트다. 인자 없이 돌리면 자기 자신을 `--server` 로 띄워 시험한다 (`posix_spawn`, Unix 소켓 — 포트 충돌 없음). 기다림은 전부 시한이 있고, 시간을 PASS/FAIL 에 쓰지 않는다.

### Part A — 할당기, 큐, 이벤트 루프 잇기
- 슬랩: `slab_alloc`, `slab_free` — 64/256/1024/4096/16384 B 클래스, 침입형 free list, 페이지 상한.
- 큐: `bq_try_push`(가득 차면 즉시 false), `bq_pop`(잠들기), `bq_close`(남은 항목은 끝까지 내줌).
- 잇기: `comp_push`(워커→루프 깨우기), `on_completions`(응답 전달, 큐 기다리던 연결 재시도).
- 검증: 조각난 요청, 파이프라인, 소켓 버퍼를 넘는 출력(EVFILT_WRITE), 다중 클라이언트, 역압, 슬랩 소진과 회복.

### Part B — 신호: 드레인과 리로드
- `drain_begin`, `drain_finished`: SIGTERM → 리슨 닫기, 접수분은 끝까지, 새 요청은 `ERR draining`.
- `cfg_acquire/release`, `cfg_publish`, `limbo_scan`: SIGHUP → 새 설정을 게시, 옛 설정은 아무 워커도 안 쥘 때 해제.
- 강제 스케줄: `PAUSE` 로 워커를 **설정을 쥔 채** 멈춘 뒤 신호를 보낸다.

### Part C — 충돌 복구: 로그
- `wal_append`, `wal_replay`: `[magic][길이][CRC][본문]` 레코드, 찢어진 꼬리 자르기, CRC 불일치에서 멈춤.
- 검증: 정상 재시작, 찢어진 꼬리 두 종류, 비트 깨짐, 부하 중 SIGKILL → ack 받은 쓰기는 전부 복구.
- 펌웨어 연결: FTL 저널, 메타데이터 로그, 전원 차단 복구와 같은 구조다.

## 2. Problem (English)

Implement the missing parts of a single-binary key-value daemon for macOS: one kqueue event-loop thread, N worker threads, a bounded request queue, a slab allocator, an append-only log, and live signal handling.

```c
void *slab_alloc(slab_t *sl, size_t n);          /* NULL if n too large or page limit reached */
void  slab_free(slab_t *sl, void *p, size_t n);
bool  bq_try_push(bq_t *q, void *item);          /* never blocks */
bool  bq_pop(bq_t *q, void **out);               /* blocks; false only when closed AND empty */
void  bq_close(bq_t *q);
void  comp_push(srv_t *s, req_t *r);             /* worker → event loop */
void  on_completions(srv_t *s);                  /* event loop thread */
void  drain_begin(srv_t *s);                     /* SIGTERM: stop accepting; idempotent */
bool  drain_finished(srv_t *s);
cfg_t *cfg_acquire(srv_t *s, int id);            /* publish hazard, re-validate */
void  cfg_release(srv_t *s, int id);
cfg_t *cfg_publish(srv_t *s, cfg_t *neu);        /* atomic exchange, returns old */
void  limbo_scan(srv_t *s);                      /* free only versions no worker has pinned */
int   wal_append(wal_t *w, int op, const char *k, size_t klen, const char *v, size_t vlen, int durability);
int   wal_replay(const char *path, kv_t *kv, replay_t *out);
```

Constraints: the event loop never blocks. A request is "accepted" once it enters the queue; every accepted request gets exactly one response, even across SIGTERM. A write is acknowledged only after its log record has been written, so every acknowledged write survives SIGKILL. A SIGHUP swaps the whole configuration atomically; a request observes one configuration version for its entire execution; a configuration object is freed only after no worker can reference it.

Example session:

```text
> SET a 1          < OK
> GET a            < VAL 1
> CFG              < CFG 1 10 20 gen1
(SIGTERM)
> GET a            < ERR draining
```

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o st && ./st
```

TODO 는 15 개(슬랩 2, 큐 3, 잇기 2, 설정 4, 드레인 2, 로그 2)다. Part A 단위 검사를 통과해야 서버 시험이 돈다. 시작 상태의 실제 실행 끝부분:

```text
SKIP  22 checks (Part A 단위 실패)
--- Part B: drain ---
SKIP  25 checks (Part A 실패)
--- Part B: 설정 리로드 ---
SKIP  15 checks (Part A 실패)
--- Part C: 로그 복구 ---
SKIP  24 checks (Part A 실패)
8/109 passed
```

종료 코드 1, 0.2 초. 서버가 안 뜨거나 응답이 없어도 모든 기다림에 시한이 있어 멈추지 않는다 (하네스 전체에는 280 초 감시 타이머, 서버에는 240 초 `alarm`).

추천 순서: 슬랩 → 큐 → `comp_push` / `on_completions` (여기까지 Part A 통과) → `drain_*` → `cfg_*`, `limbo_scan` → `wal_append`, `wal_replay`. Part B·C 는 `wal_append` 스텁이 거짓 성공을 돌려주므로 Part C 를 채우기 전까지는 복구 검사만 실패한다.

## 4. 힌트

<details><summary>힌트 1 — slab_alloc 에서 UNPOISON 을 언제?</summary>

free list 의 next 포인터는 객체 안에 산다. ASan 빌드에서 반납된 객체는 독이 칠해져 있으니, **next 를 읽기 전에** UNPOISON 해야 한다. bump 에서 잘라 줄 때도 페이지가 통째로 독이 칠해져 있으니 내주는 객체만 UNPOISON. slab_free 는 반대로: next 를 쓴 다음에 POISON.
</details>

<details><summary>힌트 2 — bq_pop 의 while 조건</summary>

`while (count == 0 && !closed) wait`. 깬 뒤에 count 가 0 이면 닫힌 것이고 false. 닫혔어도 남은 항목이 있으면 내줘야 드레인이 안 잃는다. 스프리어스 웨이크업(OS-B06)이 있으니 `if` 는 안 된다.
</details>

<details><summary>힌트 3 — 왜 "비어 있었을 때만" 깨워도 되나</summary>

이벤트 루프는 깨어나면 완료 큐를 통째로 떼어 간다. 큐가 비어 있지 않았다면 이미 누군가 트리거했고, 그 깨움을 받은 루프가 내 항목까지 가져간다. 루프가 "떼어 간 직후" 에 내가 넣으면 큐는 다시 비어 있었던 것이니 내가 깨운다. 깨움을 잃는 순서가 없다. 트리거는 락 밖에서 해도 된다.
</details>

<details><summary>힌트 4 — limbo_scan 의 조건</summary>

옛 설정 하나를 지우려면 모든 워커의 hazard 슬롯을 봐야 한다. 슬랩처럼 한꺼번에 지우지 말고, limbo 를 돌며 "슬롯에 없는 것만" 지운다. `s->limbo[i] = s->limbo[--s->limbo_n]` 로 순서 없이 제거한다.
</details>

<details><summary>힌트 5 — wal_replay 의 멈춤 조건 순서</summary>

머리 12 B 가 다 있나 → magic → 길이 범위 → 몸통이 다 있나 → CRC → `5 + klen + vlen == plen`. **길이를 믿기 전에** 범위를 먼저 본다 (쓰레기 길이로 범위 밖을 읽는 것을 막는다). 첫 실패에서 멈추고 `ftruncate(valid_end)`.
</details>

<details><summary>힌트 6 — drain_finished 가 true 인데 연결을 왜 바로 안 닫나</summary>

닫는 건 `server_main` 의 몫이다. `drain_finished` 는 "응답이 소켓까지 나갔나"(`out_len > out_off`)만 본다. 큐와 워커는 비었고 소켓 버퍼로 넘어간 바이트는 커널이 이어서 보낸다 (`close` 뒤에도 Unix 소켓은 상대가 읽을 때까지 데이터가 남는다).
</details>

## 5. 풀이

### 5.1 소유권부터 정한다

락을 언제 쓸지는 나중 문제다. 표를 먼저 그린다.

| 상태 | 주인 | 공유 방법 | 결과 |
|---|---|---|---|
| `conn_t` (fd, 입출력 버퍼, busy) | 이벤트 루프 | 없음 | 락 없음. 워커는 포인터만 들고 건드리지 않는다 |
| `inflight`, `limbo`, `stalled` 수 | 이벤트 루프 | 없음 | 락 없음 |
| `req_t` | 넘겨받는 쪽 | bq / 완료 큐의 뮤텍스 | 소유권 이동이 곧 happens-before |
| KV 표 | 공유 | 줄무늬 뮤텍스 64 개 | 키마다 잠금 범위가 다르다 |
| 슬랩 | 공유 | 클래스별 뮤텍스 | 클래스 간 경합 없음 |
| 현재 설정 | 공유 | atomic 포인터 + 불변 객체 | 리더 대기 없음 |
| WAL | 공유 | 로그 뮤텍스 (stripe 안에서) | 순서 기록 |

스레드가 소유권을 "건네는" 설계는 RTOS 태스크 사이에 메시지 큐로 버퍼를 넘기는 패턴과 같다. TSan 은 뮤텍스를 happens-before 로 본다. 슬랩이 메모리를 재활용해도 슬랩 락을 지나므로 false positive 가 없다 (TOOL-M09).

### 5.2 Part A — 잠들 수 있는 곳과 잠들면 안 되는 곳

슬랩의 핵심은 **고정 크기로 잘라 쓰고 침입형 free list 로 되돌린다** 는 것이다. RTOS 메모리 풀(메모리 파티션)과 같고, 다른 점은 클래스가 여러 개이고 페이지를 필요할 때 받아 상한까지 쓴다는 것이다. 왜 쓰나:

| | malloc | 슬랩 |
|---|---|---|
| 평균 크기 맞는 객체의 alloc | 수십~수백 ns, 전역 구조 | free list pop 1 번 |
| 메모리 상한 | OS 가 줄 때까지 | `max_pages` 에서 NULL → `ERR nomem` |
| 단편화 | 장기 실행에서 위험 | 내부 단편화(클래스 크기까지) |
| 문제 | — | **ASan 이 재활용 객체 안의 UAF 를 못 본다** |

마지막 줄이 custom allocator 의 대가다. 그래서 `POISON` / `UNPOISON` 매크로(`ASAN_POISON_MEMORY_REGION`)로 슬랩이 독을 칠한다. 칠 안 하면 재활용된 node 를 읽는 버그가 ASan 에 안 잡힌다.

큐는 OS-B06 에서 만든 bounded buffer 다. 달라진 점이 둘 있다.

1. **`push` 는 절대 막히면 안 된다.** 이벤트 루프가 막히면 연결 전체, 신호 처리까지 다 멈춘다. 그래서 try_push + 역압(큐가 차면 줄을 소비하지 않고 `stalled` 표시)으로 간다.
2. 깨우는 건 condvar, 하지만 **완료는 condvar 로 못 받는다**. 이벤트 루프는 `kevent` 에서 자고 있기 때문이다. `EVFILT_USER` + `NOTE_TRIGGER` 가 Linux eventfd 에 해당한다.

`on_completions` 는 완료 큐를 통째로 뗀다. 락을 잡고 O(1), 그 다음은 락 밖에서 처리한다. `retry_stalled` 는 왜 완료 쪽에서 돌리나? 연결이 stalled 면 큐가 찼다 → 큐에 항목이 있다 → 워커가 언젠가 끝낸다 → **완료 이벤트가 반드시 뒤따른다**. 그래서 깨울 시점을 놓치지 않는다. 같은 이유로, 슬랩 고갈로 `req_t` 를 못 받을 때는 stall 하지 않고 곧바로 `ERR nomem` 으로 거절한다. 그때는 진행 중인 요청이 하나도 없을 수 있어서 깨워 줄 완료가 안 올 수도 있다.

고정 크기 입출력 버퍼의 함정이 하나 더 있다. 출력 버퍼에 응답 하나(`MAX_RESP`)가 들어갈 자리가 없으면 **다음 줄을 파싱하지 않는다**. 그러면 `on_completions` 는 자리 검사 없이 복사해도 안전하고, 느린 클라이언트의 메모리 사용이 연결당 `OUT_CAP` 으로 묶인다. 클라이언트가 안 읽어도 서버 메모리는 안 늘어난다.

kqueue 쪽에서 한 가지 꼭 봐야 할 것: **한 번의 `kevent` 가 돌려준 배치 안에 이미 닫힌 연결의 이벤트가 남아 있을 수 있다.** 그래서 `conn_close` 는 `close(fd)` 는 바로 하되 `conn_t` 는 `zombies` 목록에 넣어 배치가 끝난 뒤 해제한다. `udata` 로 받은 포인터가 항상 유효하게 하는 장치다. 안 하면 use-after-free 가 부하 때만 나타난다.

Linux 로 옮긴다면 이렇게 대응된다 (이 레슨은 macOS 에서만 돌렸다).

| 여기 (kqueue) | Linux (epoll) |
|---|---|
| `EVFILT_READ` / `EVFILT_WRITE` | `EPOLLIN` / `EPOLLOUT` |
| `EVFILT_SIGNAL` | `signalfd` (시그널을 블록해 둬야 함) |
| `EVFILT_USER` + `NOTE_TRIGGER` | `eventfd` 에 8 B 쓰기 |
| `EV_ADD` / `EV_ENABLE` / `EV_DISABLE` | `epoll_ctl` ADD / MOD |
| `kevent` 의 `udata` | `epoll_event.data.ptr` |

### 5.3 Part B — 드레인은 상태로 정의한다

"그레이스풀" 이 뭔지 말로만 하면 테스트를 못 짠다. 요청 하나의 운명을 셋으로 나눈다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">드레인 시간표 — 요청 하나의 운명은 셋 중 하나</text>
  <line x1="20" y1="70" x2="720" y2="70" stroke="currentColor" stroke-width="2"/>
  <path d="M712 64 L722 70 L712 76" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="620" y="58" fill="currentColor">시간</text>
  <line x1="230" y1="52" x2="230" y2="88" stroke="currentColor" stroke-width="3"/>
  <text x="170" y="48" fill="currentColor" font-weight="bold">SIGTERM</text>
  <line x1="540" y1="52" x2="540" y2="88" stroke="currentColor" stroke-width="3"/>
  <text x="500" y="48" fill="currentColor" font-weight="bold">드레인 끝</text>
  <rect x="20" y="100" width="210" height="50" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="30" y="122" fill="currentColor" font-weight="bold">접수(accepted)</text>
  <text x="30" y="142" fill="currentColor">큐에 들어감 → 응답 반드시</text>
  <rect x="230" y="160" width="310" height="50" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="240" y="182" fill="currentColor" font-weight="bold">거절(rejected)</text>
  <text x="240" y="202" fill="currentColor">ERR draining — 클라이언트가 재시도 결정</text>
  <rect x="540" y="220" width="180" height="50" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="550" y="242" fill="currentColor" font-weight="bold">미응답</text>
  <text x="550" y="262" fill="currentColor">닫힌 뒤 보냄 → EOF</text>
  <text x="240" y="122" fill="currentColor">접수분 처리 계속 (큐·워커·출력 버퍼)</text>
  <line x1="240" y1="130" x2="535" y2="130" stroke="currentColor" stroke-dasharray="4 3"/>
  <g fill="currentColor">
    <text x="20" y="292">불변식: sent == ok + rejected + unanswered, 클라이언트당 unanswered ≤ 1,</text>
    <text x="20" y="312">서버 BYE 의 completed == 클라이언트 ok 합계, lost == 0. 재시작 후 ok 받은 쓰기는 전부 존재.</text>
  </g>
</svg>
```

`drain_begin` 은 리슨 소켓을 닫고 파일을 지운다(새 연결은 `connect` 실패). 큐는 건드리지 않는다. 이벤트 루프는 평소처럼 돌고, `handle_line` 이 `draining` 이면 `ERR draining` 으로 답하므로 stalled 연결도 그 경로로 빠진다. `drain_finished` 는 `inflight == 0` 이고 모든 연결의 `out` 이 비었을 때. 그 뒤 `bq_close` → 워커 조인 → `fsync` → `BYE`. 끝나지 않으면(안 읽는 클라이언트, 멈춘 워커) `drain_deadline` 이 지나 종료 코드 3 으로 나간다. 드레인은 항상 **시한이 있어야** 한다: systemd 도 SIGTERM 뒤 제한 시간이 지나면 SIGKILL 을 보낸다(기본 시간은 배포판 설정 — 90 초라는 값은 기억에 의존).

테스트는 신호를 언제 보낼지 **게이트로 강제한다**. 워커 1 개, 연결 6 개가 요청 1 개씩: `PAUSE` 후 워커가 첫 요청을 쥐고 멈추고(held=1) 나머지 5 개는 큐에 들어간다(q=5). `STATS` 로 그 상태를 **확인한 뒤** SIGTERM. 이러면 "신호 순간에 정확히 6 개가 접수되어 있었다" 가 매번 같다. 일부러 큐를 버리는 `drop-queue` fault(서버 인자) 를 넣으면 같은 시나리오가 버그를 잡는다 (실제 출력):

```text
      fault=drop-queue: 응답 1/6, 서버 종료 코드 3, BYE accepted=6 completed=1 rejected=0 lost=5 slab_in_kv_or_leak=1
```

`hold` 된 1 개만 응답을 받고 5 개는 사라진다. "테스트가 버그를 잡는지 테스트한다"(mutation)는 습관이 검증 엔지니어의 일이다 (SIV-M09). 정상 서버에서는 같은 순서로 `응답 6/6`, 종료 코드 0, 재시작 뒤 6 개 전부 복구.

부하 중에 SIGTERM (ack 400 개 이후) 의 실제 결과 (이 수치는 매번 조금씩 다르다, 하지만 불변식은 매번 성립):

```text
      부하 중 SIGTERM: sent=459 ok=455 rejected=1 unanswered=3  (BYE accepted=455 completed=455 rejected=1 lost=0 slab_in_kv_or_leak=455)
```

SIGHUP 은 다른 이야기다. 원자성의 근거는 **설정이 불변 객체**라는 것이다. 새 객체를 만들어 검증하고(`cfg_load` 실패면 게시하지 않음 — 전부 아니면 전무), `atomic_exchange` 로 포인터 하나만 바꾼다. 찢어진 읽기가 구조적으로 없다. 남는 문제는 옛 객체를 언제 지우느냐이고, 여기서 hazard slot 을 썼다.

| | EBR (OS-A03) | Hazard slot (이 레슨, LLAT-M03) |
|---|---|---|
| 리더 비용 | 진입 시 epoch 공개 | 포인터 공개 + 재검증 (seq_cst) |
| 지연 | 전 리더가 지나갈 때까지 | 그 객체를 든 리더만 |
| 여기 맞는가 | 오버킬 | 워커 수 ≤ 16, 보호 대상 1 개, 스캔이 작다 |

```c
cfg_t *c = atomic_load(&s->cfg);
for (;;) {
    atomic_store(&s->hazard[id], c);        /* 리더: "나는 이걸 쓴다" 공개 */
    cfg_t *again = atomic_load(&s->cfg);    /* 그 사이 교체됐나? */
    if (again == c) return c;
    c = again;
}
```

왜 재검증이 필요한가: 포인터를 읽은 뒤 슬롯에 쓰기 전에 writer 가 교체하고 스캔까지 끝내면, writer 는 내 슬롯을 비어 있다고 보고 지운다. 재검증이 "내 공개 뒤에도 여전히 그 포인터가 현재다" 를 보장한다. 한쪽은 `store slot → load cfg`, 다른 쪽은 `exchange cfg → load slot` 이니 정확히 OS-A03 에서 본 **SB 모양** 이다 — 그래서 seq_cst 를 쓴다.

증거가 되는 시나리오는 강제 스케줄이다. 워커가 v2 를 쥔 채 게이트에서 멈춘 동안 SIGHUP 으로 v3 게시 → `limbo=1, freed` 변화 없음 → RESUME → 그 요청 응답은 **`CFG 2 ...`** (시작할 때의 버전) → `limbo=0, freed+1`. 슬롯이 없다면 어떻게 되는가? 일부러 만든 `free-now` fault 로 보자 (같은 시나리오, ASan 빌드, 실제 보고서, 일부 생략):

```text
==58575==ERROR: AddressSanitizer: heap-use-after-free on address 0x606000000200 at pc 0x000102073ae0 ...
READ of size 4 at 0x606000000200 thread T1
    #0 0x000102073adc in worker_main solution.c:678
…
freed by thread T0 here:
    #0 0x0001027d1258 in free+0x7c (libclang_rt.asan_osx_dynamic.dylib:arm64e+0x41258)
    #1 0x00010207435c in srv_run solution.c:931
…
SUMMARY: AddressSanitizer: heap-use-after-free solution.c:678 in worker_main
```

`T1`(워커)의 읽기와 `T0`(이벤트 루프)의 해제. 스택이 그대로 원인을 보여준다. ASan 없이 같은 시나리오를 돌리면 더 나쁘다 — 서버는 멀쩡히 끝나고 클라이언트는 쓰레기를 받는다:

```text
scenario free-now: 응답 받음=1, 내용="CFG 4385 0 0 "
scenario free-now: 서버 종료 코드 0, 시그널 (없음)
```

`CFG 4385 0 0` — `beta != 2*alpha` 이고 버전도 쓰레기다. 시그널 하나 때문에 설정 객체가 재활용 메모리로 읽혔는데 서버는 잘 죽었다. 그래서 고침의 증거는 같은 시나리오를 fault 없이(`none`) 돌려 v2 응답 + ASan 깨끗함을 확인하는 것이다.

시그널은 **EVFILT_SIGNAL** 로 받았다. 핸들러가 없으니 async-signal-safe 를 고민할 게 없고, 루프 안에서 일반 함수처럼 처리한다. `signal(SIGTERM, SIG_IGN)` 인데 받을 수 있는 이유는 man kqueue 에 있다.

```text
EVFILT_SIGNAL  Takes the signal number to monitor as the identifier and
               returns when the given signal is generated for the process.
               This coexists with the signal() and sigaction() facilities, and has
               a lower precedence. Only signals sent to the process, not to a
               particular thread, will trigger the filter. The filter will record
               all attempts to deliver a signal to a process, even if the signal
               has been marked as SIG_IGN.
```

`raise()` 는 스레드 지정이라 안 잡힌다. 연습 1 에서 실측했다 (`raise()` → 이벤트 0, `kill(getpid())` ×3 → 1 이벤트, `data=3`). 표준 시그널은 큐잉되지 않으니 개수를 믿지 말고 "일어났다"만 믿어야 한다. 테스트가 SIGHUP 을 하나씩 보내고 게시를 기다리는 이유다.

### 5.4 Part C — 로그 다음에 응답

쓰기 경로의 순서가 전부다.

```text
슬랩에서 node 확보  →  stripe 락  →  WAL 기록  →  표에 연결  →  언락  →  응답
```

슬랩을 먼저 받는 이유: 로그를 쓴 뒤 메모리가 없어 실패하면 재시작할 때 그 쓰기가 되살아난다(클라이언트는 실패로 알고 있는데). 줄무늬 락 안에서 로그를 쓰는 이유: 같은 키의 두 쓰기가 **표와 로그에서 같은 순서**가 되어야 재생 결과가 같다.

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">WAL 레코드와 찢어진 꼬리</text>
  <rect x="10" y="40" width="80" height="44" class="f-acc" stroke="currentColor"/>
  <text x="20" y="66" fill="currentColor" font-family="monospace">magic</text>
  <rect x="90" y="40" width="80" height="44" class="f-acc" stroke="currentColor"/>
  <text x="100" y="66" fill="currentColor" font-family="monospace">plen</text>
  <rect x="170" y="40" width="80" height="44" class="f-acc" stroke="currentColor"/>
  <text x="180" y="66" fill="currentColor" font-family="monospace">crc</text>
  <rect x="250" y="40" width="210" height="44" class="f-ok" stroke="currentColor"/>
  <text x="258" y="66" fill="currentColor" font-family="monospace">op klen vlen key value</text>
  <text x="480" y="66" fill="currentColor">magic: 위치 확인, crc: 몸통 보호</text>
  <rect x="10" y="110" width="140" height="40" class="f-ok" stroke="currentColor"/>
  <text x="20" y="135" fill="currentColor">레코드 1</text>
  <rect x="150" y="110" width="140" height="40" class="f-ok" stroke="currentColor"/>
  <text x="160" y="135" fill="currentColor">레코드 2</text>
  <rect x="290" y="110" width="140" height="40" class="f-ok" stroke="currentColor"/>
  <text x="300" y="135" fill="currentColor">레코드 3</text>
  <rect x="430" y="110" width="70" height="40" class="f-warn" stroke="currentColor"/>
  <text x="438" y="135" fill="currentColor">찢어짐</text>
  <line x1="430" y1="100" x2="430" y2="166" stroke="currentColor" stroke-width="2" stroke-dasharray="5 3"/>
  <text x="300" y="186" fill="currentColor">valid_end: 여기까지 믿는다. ftruncate 로 뒤를 잘라낸다.</text>
  <g fill="currentColor">
    <text x="10" y="220">깨지는 지점에서 멈추는 이유: 그 뒤의 레코드를 믿을 근거가 없다(크기 필드부터 틀렸을 수 있다).</text>
    <text x="10" y="242">재생은 멱등이다: 두 번 재시작해도 상태가 같다 (테스트로 확인). SET/DEL 레코드의 순서만이 상태를 정한다.</text>
    <text x="10" y="262">펌웨어 대응: FTL 저널의 페이지 단위 CRC, 시퀀스 번호, 전원 차단 복구 때 첫 불량 페이지에서 멈춤.</text>
  </g>
</svg>
```

실제 출력 하나: 부하 중 SIGKILL 뒤 복구.

```text
      SIGKILL: ack 받은 쓰기 314 건, 재시작 복구 315 건
```

복구가 ack 보다 **많은** 건 정상이다. 로그는 썼는데 응답 전달 전에 죽은 쓰기가 있다. 반대는 있으면 안 된다. 그게 검사다 (`h2.rec >= ok`, ack 받은 키 전부 존재).

**SIGKILL 시험이 증명하지 않는 것**: `write` 는 페이지 캐시까지 간다. 프로세스가 죽어도 커널은 살아 있으니 살아남는다. 그러나 **전원이 나가면** 그 데이터는 없을 수 있다. 전원 차단 내구성은 `fsync`, macOS 는 그것조차 드라이브 캐시까지만이고 `F_FULLFSYNC` 가 있어야 한다. `man fsync` 의 한 구절:

```text
Note that while fsync() will flush all data from the host to the drive
(i.e. the "permanent storage device"), the drive itself may not
physically write the data to the platters for quite some time and it may
be written in an out-of-order sequence.
```

이 레슨의 `durability` 설정이 세 값을 고른다. 실측 (2회, 클라이언트 8 개 closed-loop, GET 80 % / SET 20 %, `demo_load.c`, `./demo_load ./so`).

측정 환경: `sysctl` 결과 Apple M6 (perflevel0/1/2 = 2/4/6 코어), `cc --version` = Apple clang 21.0.0 (clang-2100.0.123.102). 다른 작업과 맥을 공유 중이었다.
측정 직후 `uptime` 의 load averages: 1회차 `30.36 24.63 16.09`, 2회차 `26.83 24.06 15.99`.

| workers | durability | ops/s (1회차) | p50 us | p99 us | ops/s (2회차) | p50 us | p99 us |
|---|---|---|---|---|---|---|---|
| 1 | 0 (write 만) | 197531 | 34.0 | 81.0 | 153571 | 42.0 | 191.0 |
| 2 | 0 | 178735 | 38.0 | 122.0 | 212177 | 32.0 | 92.0 |
| 4 | 0 | 150744 | 45.0 | 145.0 | 190483 | 36.0 | 95.0 |
| 8 | 0 | 158919 | 45.0 | 120.0 | 148523 | 47.0 | 118.0 |
| 4 | 1 (`fsync`) | 109487 | 49.0 | 386.0 | 103574 | 50.0 | 401.0 |
| 4 | 2 (`F_FULLFSYNC`) | 1304 | 3881.0 | 40754.0 | 1426 | 3737.0 | 34078.0 |

머신이 load 26~33 으로 바쁠 때 잰 것이다. **비율만 믿는다.** 보이는 것:

- 워커를 늘려도 빨라지지 않는다 (1→8 워커가 측정 잡음 안). 이 서버는 요청 처리가 짧아 병목이 **이벤트 루프 한 스레드의 syscall**(read/write/kevent)이다. CPU 일이 무거운 서비스면 다를 것이다. 지금은 워커 수를 늘릴 이유가 없다는 게 실측의 결론이다.
- `fsync` 는 이 머신에서 처리량을 약 1.4~1.8 배만 줄인다. `F_FULLFSYNC` 는 약 100 배 이상 느리다(p50 3.8 ms). "내구성" 이라는 한 단어에 세 등급이 있다.
- 다음 층의 답: F_FULLFSYNC 비용을 그룹 커밋(여러 레코드를 모아 한 번 flush)으로 나누는 것이다. 여기선 안 만들었다.

마지막으로 **이 설계의 한계**를 말해 둔다. 로그가 끝없이 자란다 → 컴팩션이 필요 (연습 3). 모든 쓰기가 전역 로그 락을 지난다 → 그룹 커밋. 복구는 로그 전체를 메모리로 읽는다 → 스냅샷 + 증분. 연결당 동시 1 개라 파이프라인 처리량이 한 연결에선 낮다 → 순서 버퍼(연습 4).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so    # 109/109 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o san_as && ./san_as
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o san_ts && ./san_ts
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_load.c -o demo_load && ./demo_load ./so
./san_as --scenario free-now    # 일부러 만든 UAF 를 ASan 으로 본다
```

실제 결과: 일반 빌드 8회 연속 `109/109 passed`, ASan+UBSan 2회 `109/109 passed` (stderr 0 줄), TSan 2회 `109/109 passed` (stderr 0 줄). ASan·TSan 빌드에서는 서버 자식 프로세스도 같은 바이너리로 뜬다. 서버가 race 나 메모리 오류를 내면 종료 코드 검사에서 FAIL 이 난다 (SIGKILL 로 죽인 서버는 제외).

## 7. 자주 하는 실수

1. **이벤트 루프에서 막히는 호출을 쓴다** — `pthread_cond_wait` 나 블로킹 `write`. 한 연결이 느리면 전체가 멈춘다. → try_push + 비차단 소켓, 못 넘기면 다음 이벤트에.
2. **`close(fd)` 하면서 `conn_t` 도 바로 해제** — 같은 `kevent` 배치에 그 연결의 이벤트가 남아 있으면 `udata` 가 해제된 메모리를 가리킨다. → 배치 끝에 해제 (`zombies`).
3. **드레인에서 큐를 비운다 / `bq_close` 를 먼저 한다** — 접수한 요청이 사라진다. → `inflight == 0` 까지 기다린 뒤 닫는다. `drop-queue` fault 가 이 실수를 재현한다.
4. **로그를 응답 뒤에 쓴다** — 응답을 받은 쓰기가 SIGKILL 에 사라진다. → 로그 → 표 → 응답.
5. **표를 먼저 바꾸고 로그를 쓴다, 슬랩 할당 전에 로그를 쓴다** — 실패 경로에서 로그와 표가 어긋난다. → 슬랩을 먼저, 로그, 연결.
6. **설정을 복사해 쓴다, 혹은 포인터만 바꾸고 바로 free** — ASan 보고서처럼 UAF. 불변 객체 + 지연 해제.
7. **hazard 재검증을 뺀다** — `load → store` 사이에 writer 가 스캔을 끝낼 수 있다. → 슬롯 공개 뒤 다시 읽어 같을 때만 사용.
8. **SIGHUP 을 세어서 판단한다** — 표준 시그널은 합쳐진다. → 버전 번호로 끝을 확인하고, 설정 파일은 tmp + rename 으로 바꾼다.
9. **`wal_replay` 에서 `plen` 을 믿고 바로 읽는다** — 꼬리가 찢어지면 쓰레기 길이로 범위 밖 읽기. → 범위 검사, 몸통 완전성, CRC 순.
10. **슬랩의 next 를 POISON 뒤에 쓴다 / UNPOISON 전에 읽는다** — ASan 빌드에서만 즉시 터진다 (일반 빌드는 숨어 있다).

## 8. 면접 Q&A

<details><summary>Q1. 왜 이벤트 루프 + 워커 풀인가? 스레드당 연결 1 개는 왜 안 쓰나?</summary>

연결이 많고 대부분 대기 상태면 스레드당 연결은 스택과 전환 비용으로 수천 연결에서 무겁다. 이벤트 루프 하나가 많은 fd 를 보고, CPU 를 쓰는 일만 워커로 넘긴다. **꼬리질문: 왜 워커가 필요한가?** 요청 처리 안에 fsync 같은 막히는 호출이 있으면 루프 전체가 멈추기 때문이다. 이 레슨의 측정에서 워커 수가 처리량을 늘리지 않은 것도 같은 말의 뒤집기다: 요청이 짧으면 병목은 루프다. **꼬리: 워커 수는 어떻게 정하나?** 막히는 호출이 있으면 코어 수보다 많게, 전부 CPU 작업이면 코어 수 근처로. 측정으로 정한다.
</details>

<details><summary>Q2. SIGTERM 을 받았을 때 데몬이 해야 하는 일을 순서대로 말해 보세요.</summary>

① 새 연결 거부(리슨 소켓 닫기) ② 이미 접수한 요청은 끝까지 처리 ③ 새 요청은 명시적 거절 ④ 모든 응답을 내보냄 ⑤ 시한(드레인 데드라인)이 지나면 강제 종료 + 로그 ⑥ 스레드 조인, 파일 fsync. 핵심은 "접수"를 정의하는 것이다. 여기선 큐에 들어간 순간. **꼬리: 시한이 지나면?** 응답하지 못한 수를 기록하고 비정상 코드로 끝낸다. 서비스 관리자는 어차피 뒤에 SIGKILL 을 보낸다. **꼬리: 왜 조인이 필요한가?** 워커가 쓰는 메모리(설정, 슬랩)를 해제하기 전에 끝났음을 보장하려고.
</details>

<details><summary>Q3. 시그널을 어떻게 받나요? 핸들러에서 뭘 할 수 있죠?</summary>

핸들러에서는 async-signal-safe 함수만 호출 가능하다 (`write`, `_exit` 등. `malloc`, `printf`, `pthread_mutex_lock` 은 불가). 그래서 표준 해법은 **핸들러는 깨우기만 하고 일은 메인 루프에서**: self-pipe(연습 1, 파이프에 1 바이트, errno 보존) 또는 kqueue `EVFILT_SIGNAL` / Linux `signalfd`. 이 레슨은 `EVFILT_SIGNAL` 이다. C-M14 의 "ISR 은 플래그만" 과 같은 구조. **꼬리: 멀티스레드에서 시그널은 누가 받나?** 프로세스 대상 시그널은 그 시그널을 막지 않은 아무 스레드에나 배달된다. 그래서 `signalfd` 방식은 전 스레드에서 블록해 둬야 한다.
</details>

<details><summary>Q4. 원자적 설정 리로드: 왜 락 대신 포인터 교환인가?</summary>

요청마다 읽는 데이터에 읽기-쓰기 락을 걸면 모든 워커가 그 캐시 라인을 두드린다. 불변 객체 + atomic 포인터면 읽기는 포인터 한 번이고 찢어짐이 구조적으로 불가능하다. 대가는 회수 문제 — 옛 객체를 언제 지우나. **꼬리: hazard pointer vs EBR?** Hazard 는 "그 객체를 든 리더" 만 기다리고, 메모리 상한이 작다(슬롯 수). EBR 은 리더 비용이 더 작을 수 있지만 멈춘 리더 하나가 회수 전체를 막는다 (OS-A03 실측). 여기는 객체 하나, 리더 ≤ 16 이라 hazard 슬롯 스캔이 충분하다. **꼬리: 설정 파일 자체가 찢어지면?** 파일은 tmp + rename 으로 바꾸고, 그래도 쓰레기면 파서가 거부하고 옛 버전 유지(시험함).
</details>

<details><summary>Q5. 크래시 일관성: write 와 fsync 와 F_FULLFSYNC 의 차이는?</summary>

`write` 는 커널 페이지 캐시까지. 프로세스가 죽어도 남지만 전원이 나가면 사라질 수 있다. `fsync` 는 파일 데이터를 저장 장치까지 내려보내지만, macOS man 은 드라이브 내부 캐시까지만이라고 적고 있어 `F_FULLFSYNC` 가 있다. 실측 처리량은 `write` 만 쓸 때 대비 `fsync` 가 약 0.54~0.73 배, `F_FULLFSYNC` 는 100 배 이상 느림(이 머신, 부하 높음). **꼬리: Linux 는?** `fsync` 가 드라이브 플러시까지 포함한다고 알려져 있지만 파일시스템과 장치 설정에 달려 있다 (여기서 검증 안 함). **꼬리: 어떻게 비용을 줄이나?** 그룹 커밋 — 여러 스레드의 레코드를 모아 flush 한 번. **꼬리: 펌웨어로 치면?** 플래시의 페이지 프로그램 단위 쓰기와 저널, 전력 끊김 대비 capacitor(PLP) 가 있는 SSD 와 없는 SSD 의 차이다.
</details>

<details><summary>Q6. 로그가 찢어졌는지 어떻게 아나? 중간이 깨지면?</summary>

마지막 레코드가 완전하지 않으면(머리·몸통 부족) 또는 CRC 가 틀리면 거기서 멈추고 앞까지만 믿는다. 멈추는 이유는 그 뒤의 길이 필드도 믿을 수 없어서다. 꼬리의 부분 기록은 충돌의 정상 결과이고, 중간의 CRC 오류는 미디어 손상이라 의미가 다르다. **꼬리: 중간 손상도 자르나?** 이 레슨은 자르고(잘린 바이트를 보고) 계속한다. 데이터 유실을 조용히 넘기는 것이므로 제품에서는 "꼬리 말고 중간이 깨지면 기동 거부 + 운영자 호출" 이 더 안전할 수 있다. 정책 문제다. **꼬리: CRC 충돌은?** CRC32 는 무작위 오류엔 충분하지만 악의적 변조는 못 막는다. 암호학 해시는 다른 문제다.
</details>

<details><summary>Q7. 커스텀 슬랩을 쓰면 뭐가 나빠지나?</summary>

ASan 이 객체 경계를 모르게 된다: malloc 을 안 쓰니 redzone 이 없다. 재활용된 객체의 UAF 는 "정상 메모리 읽기" 가 된다. 그래서 수동 poisoning 이 필요하다. TSan 은 슬랩 락이 happens-before 를 주므로 괜찮다. 내부 단편화(요청 크기 → 클래스 크기), 클래스별 재사용 불균형(한 클래스가 메모리를 쥐고 안 돌려줌)도 있다. **꼬리: 왜 쓰나?** 크기 고정, 할당 시간 예측, 상한 보장 — RTOS 메모리 풀과 같은 이유.
</details>

<details><summary>Q8. 이런 데몬을 어떻게 테스트하나요?</summary>

(1) 순서를 강제한다: 테스트 훅으로 워커를 멈추고 STATS 로 상태를 확인한 뒤 신호를 보낸다. 시간에 의존하지 않고 같은 순서가 매번 재현된다. (2) 결과를 세는 불변식: 모든 요청의 운명이 하나, 응답 수 == 접수 수, ack 받은 쓰기 ⊆ 재시작 후 데이터. (3) mutation: 일부러 큐를 버리는 fault 로 테스트가 실패하는지 본다. (4) 부하 + 비정상 종료를 무작위로 섞는 것은 스트레스/시뮬레이션(SIV-M09, TEST-A05)의 영역이고, 이 레슨은 고정 시나리오다. **꼬리: TSan 이 못 잡는 것은?** 실행되지 않은 경로, lock-order 일부(macOS TSan), 프로세스 간 순서. 그래서 불변식 테스트가 같이 있어야 한다.
</details>

## 9. 추가 연습

각 답은 `practice.c` 에 있다.

1. **self-pipe.** `EVFILT_SIGNAL` 없는 환경의 표준 해법을 만든다. 핸들러는 파이프에 1 바이트 쓰고 errno 를 복원한다. 파이프가 가득 차면 어떻게 되나?

<details><summary>답</summary>

파이프를 `O_NONBLOCK` 으로 열어 두면 핸들러의 `write` 가 `EAGAIN` 으로 실패하고 그냥 돌아온다. 이미 깨울 바이트가 쌓여 있으니 잃는 것이 없다 — 시그널 "개수"를 세지 않는 설계만 그렇다. 연습의 검사는 200,000 번 raise 뒤에도 프로세스가 멈추지 않는 것을 본다. `errno` 복원 이유: 핸들러가 끼어든 지점에서 방금 실패한 호출의 errno 를 읽을 수 있다. `raise` 는 EVFILT_SIGNAL 에 안 잡히고 `kill(getpid())` 는 잡힌다는 것도 확인했다 (`data=3`).
</details>

2. **슬랩 이중 해제 검출.** 객체별 상태 표(할당/해제)를 두어 이중 해제와 범위 밖 포인터를 오류로 돌려준다.

<details><summary>답</summary>

해제 시 포인터를 객체 경계로 나눠 인덱스를 구하고, `(a - base) % size == 0` 이면서 상태가 "할당"일 때만 받는다. 상태 표는 객체당 1 바이트 비용이다. 슬랩 전체에 넣으면 침입형 next 를 지우는 실수까지 잡을 수 있다.
</details>

3. **컴팩션.** 로그를 재생해 살아 있는 항목만 임시 파일에 쓰고 `fsync` → `rename`. 각 지점에서 충돌했을 때 상태가 같아야 한다.

<details><summary>답</summary>

`rename` 전에 죽으면 옛 로그가 그대로이고 임시 파일은 다음 시작 때 무시/삭제. `rename` 뒤에 죽으면 새 로그가 쓰인다. 어느 쪽도 같은 키-값이다. `rename` 뒤 디렉터리 `fsync` 가 있어야 이름 변경 자체가 전원 차단을 넘는다. 이 연습의 "충돌"은 함수를 일찍 끝내는 시뮬레이션이다(전원 차단 실험 아님).
</details>

4. **순서 버퍼.** 연결당 동시 요청 여러 개를 허용하면서 응답 순서를 복원한다.

<details><summary>답</summary>

도착 순서로 번호를 매기고(`next_issue`), 완료는 번호 슬롯에 표시, 내보내기는 `next_deliver` 부터 연속으로 끝난 것만. 창이 가득 차면 파싱을 멈춘다(역압, 본문의 busy 와 같은 자리). 슬롯을 `seq % N` 으로 쓰면 N 의 배수 뒤에 같은 슬롯이 재사용되니 번호로 구분한다.
</details>

5. **히스테리시스 부하 차단.** 큐 깊이 high 이상이면 거절 시작, low 이하가 되어야 해제.

<details><summary>답</summary>

경계 하나로 켜고 끄면 부하가 경계 근처에서 흔들릴 때 켜짐/꺼짐이 빠르게 반복된다. 두 경계를 두면 전환 횟수가 줄어든다. 테스트는 wobble 순열에서 전환이 1 번임을 본다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
```

실제 결과: `40/40 passed` (ASan+UBSan, TSan 빌드 모두 `40/40 passed`). 연습 1 의 관찰 줄:

```text
      raise() 뒤 EVFILT_SIGNAL 이벤트 수: 0
      kill(getpid()) x3 뒤: events=1, data(횟수)=3
```

## 10. 핵심 정리

- 소유권을 먼저 정한다: 루프만 만지는 데이터엔 락이 필요 없다. 소유권 이동은 큐의 뮤텍스가 happens-before 를 준다.
- 이벤트 루프는 막히지 않는다. `try_push` + 역압(줄을 소비하지 않음), 완료는 `EVFILT_USER` 로 깨운다.
- 드레인 = "접수 / 거절 / 미응답" 상태 정의 + 응답 전수 세기 + 시한. `lost == 0` 이 증명이다.
- 시그널 처리 = 핸들러 없음(EVFILT_SIGNAL) 또는 깨우기만(self-pipe). 표준 시그널의 개수는 믿지 않는다.
- 설정 교체 = 불변 객체 + atomic exchange + "아무도 안 쥔 뒤" 회수(hazard slot, SB 모양이라 seq_cst).
- 쓰기 순서: 메모리 확보 → 로그 → 표 → 응답. ack 받은 쓰기는 SIGKILL 에 살아남는다. 전원 차단은 fsync 등급의 문제.
- 로그: magic/길이/CRC, 첫 불량 레코드에서 멈춤, 뒤를 자른다.
- 테스트: 훅으로 순서를 강제하고, mutation 으로 테스트를 시험한다.

## 참고자료

- `man 2 kqueue`, `man 2 kevent` (macOS) — EVFILT_READ/WRITE/SIGNAL/USER, `EV_CLEAR`, `NOTE_TRIGGER`
- `man 2 fsync`, `man 2 fcntl` (macOS) — F_FULLFSYNC 설명
- OSTEP 30장 Condition Variables (bounded buffer) → OS-B06
- M. M. Michael, *Hazard Pointers*, IEEE TPDS 15(6), 2004 → LLAT-M03; EBR → OS-A03
- POSIX.1-2017 async-signal-safe 목록 (`signal-safety(7)` on Linux)
- 관련 레슨: OS-B06(bounded buffer), OS-M02(파이프/문맥 전환), OS-A01(futex), C-M14(ISR 공유 변수), TOOL-M09(TSan), SIV-M09(스트레스 설계), TEST-A05(결정론적 시뮬레이션)
