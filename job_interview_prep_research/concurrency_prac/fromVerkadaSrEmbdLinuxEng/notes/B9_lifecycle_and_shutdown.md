# B9. 스레드의 시작과 끝 — 초기화·종료·자원 해제

> **이 노트를 읽고 나면**
> - `init` 전에 getter가 불려도 안전한 전역 상태를 설계할 수 있다 (하네스가 실제로 검사한다)
> - 벤더 호출 안에 파킹된 스레드를 깨워서 종료시키는 다섯 단계를 순서대로 쓸 수 있다
> - 자원 해제 순서를 틀려서 생기는 use-after-free를 설명하고 피할 수 있다
>
> **선행**: [B5](B5_condvar_and_bounded_queue.md)(조건 변수와 bounded queue), [B8](B8_time_and_timeouts.md)(시간 다루기 — 타임아웃 있는 대기)
>
> **이 개념을 쓰는 문제**
> - [03_event_tailer_shutdown](../03_event_tailer_shutdown/question_note) Part 1 — 주 인용원. 타임아웃 없는 벤더 호출에서 빠져나오기
> - [07_badge_audit_dedupe](../07_badge_audit_dedupe/question_note) Part 1 — 생산자 4개 + 로거 1개의 종료 순서
> - [08_battery_energy_pipeline](../08_battery_energy_pipeline/question_note) Part 1 — init 전 getter, timed wait을 broadcast로 깨우기
> - [01_gps_fix_cache](../01_gps_fix_cache/question_note) · [02_modem_rssi_window](../02_modem_rssi_window/question_note) Part 1 — "첫 호출은 안 막힌다" 트릭

---

## 1. 왜 이게 필요한가

동시성 문제를 다 풀어 놓고 마지막 두 줄에서 FAIL이 나는 경험을 하게 된다. 하네스가 재는 항목이 정확성만이 아니기 때문이다. 08번 하네스의 **첫 줄**부터가 이것이다.

```
== phase 0: getters before init (static state must be safe) ==
  empty history
  get_latest before init returns != 0
```

`bms_pipeline_init()`을 **부르기 전에** getter를 호출한다. 여기서 크래시하거나 쓰레기 값을 내놓으면 뒤의 모든 검사가 무의미하다. 마지막 페이즈는 종료다. 03번 하네스는 sampler를 일부러 벤더 호출 안에 2.5초 파킹시킨 뒤 `evq_deinit()`을 부르고 **걸린 시간을 잰다** — `evq_deinit() returned in %llu us while the vendor call was parked (bound 400000)`, 그리고 `second evq_deinit() is a no-op`.

플래그만 내리는 종료는 여기서 죽는다. 벤더가 2.5초 동안 이벤트를 안 주면 스레드는 2.5초 동안 플래그를 **볼 기회가 없다**. 08번도 같은 검사를 한다 — `deinit woke the timed wait instead of waiting it out (< 1.5 s)`. 그래서 이 노트가 다루는 것은 세 구간이다. **init 전**: 전역 상태의 기본값이 "아직 안 열렸다"를 안전하게 표현하는가. **init**: 첫 값을 어떻게 채우고, 실패하면 어떻게 되돌리고, 두 번 불리면 어떻게 되는가. **deinit**: 파킹된 스레드를 어떻게 깨우고, 어떤 순서로 join하고 해제하는가.

---

## 2. 그림으로 먼저

플래그만 내린 종료와, 깨우기까지 한 종료의 타임라인이다. 숫자는 실제 측정값이다.

```svg
<svg viewBox="0 0 660 250" role="img" aria-label="플래그만 내린 종료와 플래그+wake 종료의 타임라인 비교">
  <text x="10" y="18" class="lbl">v0: 플래그만 내린다</text>
  <line class="muted" x1="10" y1="30" x2="650" y2="30"/>
  <rect class="box" x="60" y="44" width="400" height="34" rx="6"/>
  <text x="260" y="66" text-anchor="middle">sampler: vendor_read() 안에 파킹</text>
  <line class="dash" x1="140" y1="30" x2="140" y2="118"/>
  <text class="lbl" x="144" y="100">deinit: running = false</text>
  <text class="lbl" x="144" y="114">(스레드는 이 store를 볼 수 없다)</text>
  <line class="accent" x1="140" y1="88" x2="458" y2="88"/>
  <polygon class="accent" points="462,88 452,83 452,93"/>
  <text class="lbl" x="300" y="84" text-anchor="middle">pthread_join 이 여기까지 막힌다</text>
  <line class="dash" x1="460" y1="30" x2="460" y2="118"/>
  <text x="470" y="66">773 ms</text>
  <text x="10" y="148" class="lbl">v1: 플래그 → wake → join</text>
  <line class="muted" x1="10" y1="160" x2="650" y2="160"/>
  <rect class="box" x="60" y="174" width="110" height="34" rx="6"/>
  <text x="115" y="196" text-anchor="middle">파킹</text>
  <rect class="fill-soft" x="170" y="174" width="60" height="34" rx="6"/>
  <text x="200" y="196" text-anchor="middle">-2</text>
  <line class="dash" x1="140" y1="160" x2="140" y2="240"/>
  <text class="lbl" x="144" y="230">running = false; vendor_wake()</text>
  <line class="accent" x1="140" y1="216" x2="226" y2="216"/>
  <polygon class="accent" points="230,216 220,211 220,221"/>
  <line class="dash" x1="230" y1="160" x2="230" y2="240"/>
  <text x="240" y="196">12.5 ms</text>
  <text class="lbl" x="300" y="230">벤더 호출이 -2 로 끊기고 루프가 break → join 즉시 완료</text>
</svg>
```

수명 전체를 상태 기계로 보면 getter가 어떤 구간에서 어떻게 답해야 하는지가 정리된다.

```
   [ BSS / static ]          init()            [ RUNNING ]         deinit()        [ STOPPED ]
   stopping = true    ──────────────────►   stopping = false  ──────────────────►  stopping = true
   head = tail = 0     첫 샘플(막지 않으면)     스레드 N개 동작      flag→wake→join      스레드 0개
   ─────────────────                        ─────────────────                     ─────────────────
   getter: 실패/빈 값                        getter: 진짜 값        getter: 정책대로
   크래시 금지                                                      03 → 실패 / 08 → 계속 유효
```

---

## 3. 개념 (용어를 하나씩)

### static storage duration과 0 초기화

파일 스코프 변수나 `static` 지역 변수는 **프로그램 시작 전에** 초기화되고, 초기값을 안 적으면 0(포인터는 NULL, `bool`은 false)으로 채워진다. C 표준이 보장한다. 덕분에 `main()`이 시작되기 전에 getter가 불려도 읽는 값이 쓰레기는 아니다.

문제가 되는 것은 0이 "아직 안 열렸다"를 뜻하지 않는 경우다. `bool stopping;`을 그냥 두면 기본값 false = "동작 중"이 되어 버린다. 03번 답안은 이걸 **명시적으로 뒤집는다.**

```c
/* 03_event_tailer_shutdown/event_queue_solution.c */
} g_q = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .cv = PTHREAD_COND_INITIALIZER,
    .stopping = true,                /* a getter before evq_init() must fail, not crash */
};
```

`PTHREAD_MUTEX_INITIALIZER`는 "정적으로 초기화된 mutex"라는 매크로 상수다. `pthread_mutex_init`을 부를 필요가 없으므로 **init 전에도 lock을 잡을 수 있다**. 이게 phase 0 검사를 통과하는 방법이다.

### 멱등성 (idempotency)

여러 번 불러도 한 번 부른 것과 결과가 같은 성질. `init`/`deinit`은 무조건 멱등이어야 한다. 시스템 종료 경로가 여러 개면 `deinit`이 두 번 불릴 수 있고, 두 번째에 이미 join된 `pthread_t`를 또 join하면 **정의되지 않은 동작**이다. 구현은 `static bool g_started;` 하나면 된다. 이 변수는 init/deinit을 부르는 한 스레드만 건드리므로 atomic이 아니어도 된다 (답안 주석: *"only touched by init/deinit"*).

### join과 detach

`pthread_join(th, NULL)`은 그 스레드가 끝날 때까지 **막고** 자원을 회수한다. `pthread_detach(th)`는 끝나면 알아서 회수하게 하지만 끝났는지 알 방법이 없다. **종료에는 반드시 join을 쓴다.** detach한 스레드는 "이제 아무도 이 버퍼를 안 쓴다"를 증명할 수 없어서 해제를 언제 해도 되는지 알 수 없다.

### 파킹(parked)과 깨우기(wake) vs 취소(cancel)

**파킹**은 스레드가 우리 코드가 아닌 라이브러리 함수 안에서 블로킹되어 있는 상태다. `evsrc_read_blocking()`은 타임아웃 인자가 아예 없어서 조용한 현장에서는 수 분 걸린다. 그 동안 이 스레드는 우리 루프 조건을 검사하지 않는다. 그래서 플래그가 안 통한다 — 이것이 03번 문제의 전부다.

**wake**는 블로킹 중인 호출을 실패/특수 반환으로 끊어 주고, 스레드는 정상 경로로 계속 달린다. **cancel**(`pthread_cancel`)은 스레드를 강제로 끝내며 취소 지점에서 언제 죽을지 모른다. wake는 안전하고 cancel은 위험하다(§5 다).

---

## 4. 코드로 보기

### init — 첫 값을 동기로 가져오는 요령

원본 Verkada 문제의 ALS 센서와 01·02번 벤더에는 공통 특징이 있다: **첫 호출은 즉시 돌아온다.** 그래서 sampler 스레드를 만들기 **전에**, 호출자 스레드에서 한 번 읽어 둔다.

```c
/* 01_gps_fix_cache/gps_cache_solution.c */
int gps_cache_init(void)
{
    if (g_started)
        return 0;                    /* 멱등: 두 번째 호출은 아무 일도 안 한다 */

    /* The first vendor call returns immediately, so take it on this thread:
     * a caller that polls right after init already has a position instead of
     * waiting out a receiver timeout.  No overlap with the sampler thread --
     * it does not exist yet, which is the only reason this is safe. */
    struct GpsFix f = gps_wait_for_fix();
    handle_fix(&f);

    atomic_store_explicit(&g_running, true, memory_order_release);
    if (pthread_create(&g_thread, NULL, sampler_main, NULL) != 0) {
        atomic_store_explicit(&g_running, false, memory_order_release);
        return -1;                   /* 되돌리기: 플래그를 원복한다 */
    }
    g_started = true;
    return 0;
}
```

**왜 이게 thread-safe한가**: 벤더 함수는 "동시에 한 스레드만"이라는 제약이 있는데, 여기서는 sampler 스레드가 **아직 존재하지 않는다**. `pthread_create`가 그 뒤에 있으므로 이 호출은 스레드 생성보다 happens-before이고 겹칠 수가 없다. 주석이 그 논리를 그대로 적어 놓았다.

**이 줄이 없으면?** 동기 첫 읽기를 빼면 `init()` 직후의 getter가 -1을 돌려준다. 08번 하네스가 FAIL로 잡는다 — `get_latest has a sample right after init`. 02번은 에러까지 고려해 `for (int tries = 0; tries < 3; tries++)`로 세 번까지 재시도한다. **상한이 핵심**이다. 무한 재시도는 init을 블로킹으로 만들고, "non-blocking wrapper"를 만들라고 했는데 init이 막히면 문제를 못 푼 것이다.

### 반대로, 첫 읽기를 하면 안 되는 경우

03번은 동기 첫 읽기를 **일부러 하지 않는다.** 벤더 호출에 타임아웃이 없기 때문이다.

```c
/* 03_event_tailer_shutdown/event_queue_solution.c */
/* Note what is NOT here: a synchronous first read. evsrc_read_blocking()
 * has no timeout, so priming the state in init could hang the caller for
 * as long as the site stays quiet. Consumers get "empty" instead, which is
 * the honest answer. */
```

**판단 기준 한 줄**: 벤더의 첫 호출이 **상한 있는 시간** 안에 돌아오면 init에서 당긴다. 상한이 없으면 절대 당기지 않는다. 08번 BMS는 첫 호출이 즉시 돌아오므로 당기고, 03번 이벤트 브리지는 몇 분일 수 있으므로 안 당긴다.

### init 실패 시 되돌리기 — 08번의 2단계 롤백

스레드를 두 개 만드는 경우, 두 번째 생성이 실패하면 **첫 번째를 정리하고 나가야** 한다.

```c
/* 08_battery_energy_pipeline/energy_store_solution.c */
if (pthread_create(&sampler_th, NULL, sampler_main, NULL) != 0) {
    atomic_store(&running, false);
    return -1;
}
if (pthread_create(&uploader_th, NULL, uploader_main, NULL) != 0) {
    atomic_store(&running, false);
    pthread_cond_broadcast(&q_cv);      /* sampler가 대기 중일 수 있다 */
    pthread_join(sampler_th, NULL);     /* 만들었던 것만 정리 */
    return -1;
}
threads_up = true;
return 0;
```

`threads_up = true`가 **맨 마지막**에 있다는 점을 보라. 중간에 실패하면 이 값은 false로 남고, `deinit`의 첫 줄 `if (!threads_up) return;`이 아무 일도 하지 않는다. "성공했을 때만 참이 되는 플래그"가 롤백 로직의 절반을 공짜로 해결해 준다.

---

## 5. 단계별로 만들어 보기

### (가) v0 — 플래그만 내린다 (틀렸다)

```c
static void *loop(void *a) {
    (void)a;
    while (atomic_load(&running)) {
        if (vendor_read() == -2) break;      /* 800 ms 블로킹 */
    }
    return NULL;
}

void deinit_v0(void) {
    atomic_store(&running, false);
    pthread_join(th, NULL);                  /* 여기서 오래 막힌다 */
}
```

벤더가 800 ms 블로킹하고 스레드가 그 안에 200 ms 들어가 있는 상태에서 종료를 건 측정값이다.

```
vendor_read() blocks 800 ms; thread parked 200 ms in
v0 flag only    : deinit took 773847 us
```

**773 ms.** 03번 하네스의 상한은 400 ms이고 벤더 파킹은 2.5초다. 여기서 확실히 FAIL이다.

### (나) v1 — 플래그 다음에 깨운다 (맞다)

```c
void deinit_v1(void) {
    atomic_store(&running, false);    /* 1. 먼저 플래그. 순서가 중요하다 */
    vendor_wake();                    /* 2. 벤더의 비상구 */
    pthread_join(th, NULL);           /* 3. 이제 금방 끝난다 */
}
```

```
v1 flag + wake  : deinit took 12535 us
```

**12.5 ms**, 62배 빨라졌다. 03번 답안이 이 모양이다.

```c
/* 03_event_tailer_shutdown/event_queue_solution.c */
atomic_store_explicit(&g_running, false, memory_order_release);
evsrc_wake();

pthread_mutex_lock(&g_q.lock);
g_q.stopping = true;
pthread_cond_broadcast(&g_q.cv);
pthread_mutex_unlock(&g_q.lock);

pthread_join(g_thread, NULL);
g_started = false;
```

**플래그를 wake보다 먼저 내려야 하는 이유**: 순서를 바꾸면 `evsrc_wake()`로 깨어난 스레드가 `running`이 아직 true인 것을 보고 벤더 호출을 **한 번 더** 시작할 수 있다. 03번 벤더는 wake가 latch돼서 그 호출도 -2를 주지만 모든 벤더가 그렇지는 않다. 답안 주석이 짚는 부분이다 — latch가 없으면 `wake + timed-join`을 루프로 돌려야 한다.

### (다) 깨우는 방법 카탈로그

| 방법 | 어떻게 | 쓰는 문제 | 한계 |
| --- | --- | --- | --- |
| 벤더의 wake/cancel 함수 | `evsrc_wake()` / `reader_cancel(door)` | 03, 07 | 벤더가 줘야 있다. latch 여부를 확인할 것 |
| 타임아웃 있는 대기로 바꾼다 | `pthread_cond_timedwait`, `poll(fds, n, ms)` | 08(토큰 버킷), 10(monitor) | 최대 타임아웃만큼 종료가 늦는다 |
| condvar broadcast | flag 세팅 후 lock 잡고 `broadcast` | 03, 07, 08, 10 | 우리 코드 안에서 자는 스레드만 깬다 |
| self-pipe | `pipe()`를 `poll`에 같이 넣고 종료 때 1바이트 write | (fd 기반 벤더) | fd를 기다리는 경우에만 |
| 그냥 기다린다 | flag + join. 벤더 블로킹이 짧고 상한이 있을 때 | 01, 02 (~100 ms) | 벤더 블로킹 시간만큼 종료가 늦는다 |
| `pthread_cancel` | 강제 종료 | **쓰지 않는다** | 아래 참고 |

self-pipe는 fd를 기다리는 루프에 "종료"라는 이벤트를 하나 더 끼워 넣는 기법이다. 핵심 세 줄이다.

```c
static int wake_fd[2];   /* pipe(wake_fd); 양쪽 O_NONBLOCK. [0] read, [1] write */
struct pollfd pf[2] = { { data_fd, POLLIN, 0 }, { wake_fd[0], POLLIN, 0 } };
if (poll(pf, 2, -1) < 0) break;                          /* 타임아웃 없이 무한 대기 */
if (pf[1].revents & POLLIN) break;                       /* 종료 신호가 왔다 */
/* deinit 쪽: */  (void)!write(wake_fd[1], "x", 1);
```

실제로 돌려 보면 타임아웃 `-1`(무한)로 잠든 스레드가 파이프 한 바이트에 즉시 깬다 — `poll(-1) left in 351 us, woke_by_pipe=1`.

**`pthread_cancel`은 왜 위험한가.** 하나, 스레드가 **취소 지점(cancellation point)**에 도달해야 죽으므로 어느 줄에서 죽을지 모른다. 둘, mutex를 잡은 채로 죽으면 그 mutex는 **영원히 잠긴 상태**로 남는다(`pthread_cleanup_push`로 풀 수는 있지만 모든 경로에 다 달아야 한다). 셋, 벤더 라이브러리 내부에서 죽으면 그쪽 내부 상태가 어떻게 남는지 아무도 보장하지 않는다. 그래서 이 저장소의 어느 답안도 쓰지 않는다. 면접에서 물으면 "we ask the thread to stop and we wake it; we never cancel it."

### (라) 종료 5단계와 그 순서

```
1. flag        atomic_store(&running, false)        더 이상 새 작업을 시작하지 마라
2. wake        vendor_wake() / cond_broadcast()     블로킹에서 빠져나오게 한다
3. 탈출        (스레드들이 루프를 break한다)          — 우리가 하는 일이 아니다, 기다린다
4. join        pthread_join(th, NULL)               "이제 아무도 안 쓴다"의 유일한 증명
5. 해제        free / destroy / close                4 다음에만 안전하다
```

07번 답안이 이 다섯 단계에 "누구부터 join하느냐"까지 더한 모범 예시다.

```c
/* 07_badge_audit_dedupe/audit_log_solution.c */
atomic_store_explicit(&g_running, false, memory_order_release);   /* 1 */
for (int d = 0; d < AC42_DOORS; d++)
    reader_cancel((uint8_t)d);                                   /* 2: 벤더 비상구 */
pthread_mutex_lock(&q.m);
q.running = false;
pthread_cond_broadcast(&q.not_full);                             /* 2: 우리 대기자 */
pthread_cond_broadcast(&q.not_empty);
pthread_mutex_unlock(&q.m);
for (int d = 0; d < g_producers_made; d++)
    pthread_join(g_producer[d], NULL);                           /* 4: 생산자 먼저 */
pthread_join(g_logger, NULL);                                    /* 4: 소비자 나중 */
```

**생산자를 먼저 join하는 이유**: 생산자가 다 죽은 뒤에야 "큐에 더 들어올 것이 없다"가 확정된다. 그러면 로거는 남은 것을 안심하고 비우고 나올 수 있다. 순서를 뒤집으면 로거가 나간 뒤에 생산자가 push한 감사 기록이 사라진다.

### (마) 순서를 바꾸면 — use-after-free

```svg
<svg viewBox="0 0 640 210" role="img" aria-label="join 전에 free 하면 발생하는 use-after-free">
  <text x="10" y="18" class="lbl">틀린 순서: free → join</text>
  <line class="muted" x1="10" y1="30" x2="630" y2="30"/>
  <rect class="box" x="60" y="44" width="150" height="32" rx="6"/>
  <text x="135" y="65" text-anchor="middle">worker: buf[0]++</text>
  <rect class="box" x="210" y="44" width="120" height="32" rx="6"/>
  <text x="270" y="65" text-anchor="middle">usleep(50ms)</text>
  <rect class="fill-soft" x="330" y="44" width="170" height="32" rx="6"/>
  <text x="415" y="65" text-anchor="middle">buf[0]++ ← 해제된 메모리</text>
  <line class="dash" x1="330" y1="30" x2="330" y2="104"/>
  <text class="lbl" x="60" y="98">deinit: running=false; free(buf); join(...)</text>
  <text class="lbl" x="340" y="98">heap-use-after-free (ASan, exit 134)</text>
  <text x="10" y="140" class="lbl">맞는 순서: join → free</text>
  <line class="muted" x1="10" y1="152" x2="630" y2="152"/>
  <rect class="box" x="60" y="166" width="150" height="32" rx="6"/>
  <text x="135" y="187" text-anchor="middle">worker: buf[0]++</text>
  <rect class="box" x="210" y="166" width="120" height="32" rx="6"/>
  <text x="270" y="187" text-anchor="middle">usleep(50ms)</text>
  <rect class="box" x="330" y="166" width="110" height="32" rx="6"/>
  <text x="385" y="187" text-anchor="middle">루프 종료</text>
  <line class="dash" x1="440" y1="152" x2="440" y2="200"/>
  <text class="lbl" x="448" y="187">join 리턴 → 여기서 free</text>
</svg>
```

같은 프로그램을 순서만 바꿔 AddressSanitizer로 돌린 결과다.

```sh
cc -std=c11 -g -fsanitize=address -pthread -o uaf uaf.c
./uaf good      # join → free  →  "join-then-free order: done"
./uaf           # free → join
# ==11774==ERROR: AddressSanitizer: heap-use-after-free on address 0x6020000000f0
# SUMMARY: AddressSanitizer: heap-use-after-free      (exit=134)
```

**해제 순서의 일반 규칙: 가장 늦게 만든 것부터 되돌린다.** 스레드가 가장 늦게 생겼으니 가장 먼저 사라져야 하고, mutex/condvar는 그 스레드가 쓰던 것이니 그다음이고, 버퍼는 마지막이다. `pthread_mutex_destroy`/`pthread_cond_destroy`는 **아무도 그것을 기다리고 있지 않을 때만** 호출할 수 있다. 대기자가 있는 condvar를 destroy하면 정의되지 않은 동작이다. 그래서 join 다음이다. 정적으로 초기화한(`PTHREAD_MUTEX_INITIALIZER`) mutex는 destroy를 생략해도 되고, 이 저장소 답안들이 그렇게 한다 — 전역 수명이라 프로세스가 끝날 때까지 살아 있어야 하기 때문이다.

### (바) deinit 후 getter는 무엇을 반환해야 하나

정답은 하나가 아니다. **문제가 요구하는 계약을 보고 결정하고, 주석에 적는다.**

- **03번 — 실패로 닫는다.** `g_q.stopping = true`로 되돌려 `evq_try_pop`이 -1을 준다. 이벤트 큐는 "지금 살아 있는 스트림"이므로 닫힌 뒤의 pop은 의미가 없다.
- **08번 — 계속 유효하다.** 주석: *"History and counters are deliberately kept: `energy_joules_between()` still answers after shutdown."* 에너지 적분은 과거에 대한 질의이고, 카운터는 무슨 일이 있었는지 설명하는 증거다.
- **01번 — 그대로 둔다.** *"State is left intact on purpose: the getters stay valid after deinit."*

공통 요건은 하나뿐이다: **크래시하지 않고, 쓰레기 값을 주지 않는다.** 그리고 `deinit` 후에도 상태를 남기려면 그 상태를 **해제하지 말아야** 한다 — 위의 해제 규칙과 충돌하지 않게 미리 정해 두는 것이 설계다.

---

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
| --- | --- | --- | --- |
| `bool stopping;`을 기본값(false)으로 둔다 | init 전 getter가 빈 버퍼에서 쓰레기를 반환 | static 0 초기화 = false = "동작 중"으로 읽힌다 | `.stopping = true`로 명시 초기화 (03번) |
| `deinit`에서 플래그만 내리고 join | join이 벤더 블로킹만큼 막힌다 (측정 773 ms, 03번 상한 400 ms) | 파킹된 스레드는 루프 조건을 검사하지 않는다 | flag → wake(벤더 함수/broadcast) → join |
| wake를 플래그보다 먼저 부르거나 broadcast를 lock 없이 보낸다 | 드물게 deinit이 타임아웃 전체를 기다린다 (lost wakeup) | 대기자가 조건 재검사 후 아직 안 잠들었을 때 신호가 날아간다 | flag 세팅과 broadcast를 같은 lock 구간에서 (08번) |
| `join` 전에 `free`/`destroy` | 간헐적 크래시, ASan heap-use-after-free, exit 134 | 스레드가 아직 그 메모리를 쓰고 있다 | 항상 join 다음에 해제. 가장 늦게 만든 것부터 |
| 소비자를 생산자보다 먼저 join | 큐에 남은 감사 기록이 사라진다 (07번) | 소비자가 나간 뒤에도 생산자가 push한다 | 생산자 전부 join → 그다음 소비자 |
| `deinit`이 멱등이 아니다 | 두 번째 호출에서 이미 join된 스레드를 또 join → UB | `pthread_t`는 join 후 무효 | 첫 줄 `if (!g_started) return;`, 끝에 `g_started = false` |
| init에서 상한 없는 벤더 호출을 당겨 온다 | `init()`이 수 분 막힌다 = non-blocking wrapper 실패 | `evsrc_read_blocking()`에는 타임아웃이 없다 | 첫 호출이 상한 있을 때만 당긴다 (01·02·08 O / 03 X) |
| `pthread_cancel`로 종료 | mutex가 영원히 잠기거나 벤더 내부 상태가 깨진다 | 어느 취소 지점에서 죽는지 모른다 | 절대 쓰지 않는다. wake + join |

---

## 7. 손으로 확인하기

```sh
cd 03_event_tailer_shutdown && ./main.sh sol
# -- graceful shutdown while the sampler is inside the vendor call --
# ok  evq_deinit() returned in NNNN us while the vendor call was parked (bound 400000)
# ok  second evq_deinit() is a no-op (NN us)
```

`event_queue_solution.c`의 `evsrc_wake();` 한 줄을 주석 처리하고 다시 돌려 본다. 그 검사만 FAIL로 바뀌면서 `dt`가 2초 넘게 찍힌다. **그 숫자가 이 노트의 전부다.**

```sh
cd 08_battery_energy_pipeline && ./main.sh sol
# == phase 0: getters before init (static state must be safe) ==
# == phase 5: shutdown ==  "deinit took N us"  (N < 1500000)
cd 07_badge_audit_dedupe && ./main.sh sol
# reader_cancel 4회 → broadcast → 생산자 join → 로거 join
```

phase 0을 깨 보려면 `bms_get_latest`의 `latest_valid` 검사를 지우면 된다. 바로 FAIL이 뜬다. 해제 순서는 `free`와 `pthread_join`의 순서만 바꾼 20줄짜리 프로그램을 `-fsanitize=address`로 돌려 확인한다. 워커가 `usleep` 중일 때 `free`가 일어나도록 슬립을 50 ms 정도 주면 재현이 안정적이다.

### 체크리스트

- [ ] init 전 getter가 안전한 이유를 static 초기화로 설명할 수 있다
- [ ] 첫 샘플을 init에서 당겨올지 판단하는 기준을 한 문장으로 말할 수 있다
- [ ] 종료 5단계를 순서대로 쓰고, 각 단계가 없으면 무슨 일이 생기는지 말할 수 있다
- [ ] `deinit`을 두 번 불러도 안전하게 만들고, 생산자·소비자의 join 순서를 정할 수 있다

---

## 8. 자가 점검

```check
Q: `init()`을 부르기 전에 getter가 불릴 수 있다. 크래시하지 않게 하려면 무엇을 해야 하는가?
A: 전역 상태를 static storage duration으로 두고 "아직 열리지 않았다"를 뜻하는 값으로 명시 초기화한다. mutex는 `PTHREAD_MUTEX_INITIALIZER`로 정적 초기화해서 init 전에도 lock을 잡을 수 있게 한다. 03번 답안은 `.stopping = true`로 시작해 init 전 pop이 -1을 돌려주게 만든다. 기본값 0(false)에 의존하면 "동작 중"으로 읽혀 빈 버퍼를 읽는다.
```

```check
Q: 01·02·08번은 init에서 벤더를 한 번 동기로 읽는데 03번은 읽지 않는다. 기준은 무엇인가?
A: 벤더의 첫 호출이 상한 있는 시간 안에 돌아오는지다. GPS·모뎀·BMS는 첫 호출이 즉시 돌아오므로 호출자 스레드에서 당겨와 init 직후의 getter가 진짜 값을 갖게 한다. `evsrc_read_blocking()`은 타임아웃이 아예 없어 조용한 현장에서 수 분 걸릴 수 있으므로, 당기면 init 자체가 블로킹이 된다. 03번은 대신 "비었다"를 정직하게 돌려준다. 덧붙여 그 호출이 "한 스레드만 벤더에 진입" 제약을 어기지 않는 이유는 `pthread_create`보다 앞에 있어서 sampler가 아직 존재하지 않기 때문이다.
```

```check
Q: 스레드가 벤더 호출 안에 파킹돼 있으면 `atomic_store(&running, false)`가 왜 효과가 없는가? 그리고 종료 다섯 단계에서 4번과 5번을 바꾸면?
A: 플래그는 루프 조건에서만 읽히고, 파킹된 스레드는 벤더 함수 안에 있어 루프로 돌아오지 못한다. 측정에서 800 ms 벤더에 대해 deinit이 773 ms 걸렸다. 다섯 단계는 1) 플래그를 내린다 2) 깨운다(벤더 wake 또는 cond_broadcast) 3) 대기자들이 루프를 빠져나온다 4) join한다 5) 해제한다. 4와 5를 바꾸면 아직 살아 있는 스레드가 이미 해제된 메모리를 만져 use-after-free가 된다. ASan으로 돌리면 heap-use-after-free로 exit 134가 뜬다. join의 리턴이 "이제 아무도 이 메모리를 쓰지 않는다"의 유일한 증명이다.
```

```check
Q: 플래그를 내리는 것보다 `evsrc_wake()`를 먼저 부르면 무엇이 잘못되는가? 그리고 broadcast를 lock 없이 보내면?
A: 전자는 깨어난 스레드가 아직 true인 플래그를 보고 벤더 호출을 한 번 더 시작할 수 있다. 03번 벤더는 wake가 latch돼서 결과적으로 살지만, latch가 없으면 종료가 한 블로킹 주기 늦어지고 wake + timed-join 루프가 필요하다. 후자는 lost wakeup이다. 대기자가 조건을 재검사해 "자야겠다"고 결정한 직후 아직 잠들기 전에 신호가 날아가면 아무도 받지 못하고 타임아웃 전체를 잔다. 08번 답안이 flag 세팅과 broadcast를 같은 `q_lock` 구간에 넣은 이유다.
```

```check
Q: `deinit`을 멱등하게 만드는 방법은? 그리고 `pthread_cancel`이 위험한 이유 세 가지는?
A: `static bool g_started;`를 두고 첫 줄에서 `if (!g_started) return;`, 마지막에 `g_started = false`로 내린다. 이 변수는 init/deinit을 부르는 스레드 하나만 만지므로 atomic이 아니어도 된다. 안 그러면 두 번째 호출이 이미 join된 `pthread_t`를 다시 join해서 정의되지 않은 동작이 된다. 03번 하네스가 `second evq_deinit() is a no-op (< 50 ms)`로 검사한다. cancel이 위험한 이유는 (1) 취소 지점에 도달해야 죽으므로 어느 줄에서 죽을지 모른다 (2) mutex를 잡은 채 죽으면 영원히 잠긴 채 남는다 (3) 벤더 내부에서 죽으면 그쪽 상태를 아무도 보장하지 않는다.
```

```check
Q: 07번은 생산자 4개와 로거 1개를 join하는데 순서가 왜 중요한가? 그리고 deinit 후 getter는 무엇을 반환해야 하는가?
A: 생산자를 먼저 전부 join해야 "큐에 더 들어올 것이 없다"가 확정되고, 그러면 로거가 남은 항목을 비우고 empty+closed를 보고 나올 수 있다. 뒤집으면 로거 종료 후 생산자가 push한 감사 기록이 사라진다. getter 정책은 계약에 따라 다르다 — 03번은 `stopping = true`로 닫고 -1, 08번은 에너지 적분과 카운터를 의도적으로 유지, 01번도 유효하게 남긴다. 공통 요건은 크래시하지 않고 쓰레기 값을 주지 않는 것, 그리고 남기기로 했다면 그 상태를 해제하지 않는 것이다.
```

---

## 9. 요약 카드

| 단계 | 해야 할 일 | 안 하면 |
| --- | --- | --- |
| static 초기화 | "아직 안 열렸다"를 명시 값으로. mutex는 `PTHREAD_MUTEX_INITIALIZER` | init 전 getter가 쓰레기/크래시 |
| init 첫 샘플 | 벤더 첫 호출에 **상한이 있을 때만** 동기로 당긴다 | 값이 -1이거나 init이 블로킹 |
| init 멱등 / 실패 | `if (g_started) return 0;` · 만든 것만 정리하고 성공 플래그는 마지막에 | 스레드 중복 생성, 반쪽 상태 |
| deinit 1 | flag ← false (**wake보다 먼저**) | 한 주기 더 돈다 |
| deinit 2 | wake: 벤더 함수 / lock 잡고 broadcast | join이 벤더 블로킹만큼 막힌다 |
| deinit 4 | `pthread_join` — 생산자 먼저, 소비자 나중 | 데이터 유실 |
| deinit 5 | 해제: 가장 늦게 만든 것부터 | use-after-free |
| deinit 멱등 | `if (!g_started) return;` | 두 번째 join = UB |

영어로 말할 문장 셋:

- "A flag is not enough when the thread is parked in a vendor call with no timeout: stop flag, then wake, then join."
- "`pthread_join` returning is the only proof that nobody touches that memory any more, so nothing is freed before it."
- "We ask the thread to stop and we wake it. We never cancel it."
