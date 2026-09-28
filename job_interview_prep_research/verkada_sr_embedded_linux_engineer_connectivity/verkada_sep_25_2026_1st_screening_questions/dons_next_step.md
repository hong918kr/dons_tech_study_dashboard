# 다음 단계 — 이 유형을 확실히 손에 넣기

> **왜 이 문서가 있나**: 2026-09-25 Verkada 1차에서 나온 문제는 준비했던 유형(bounded queue, double buffer, testable driver)이 **아니었다.**
> 실제로 나온 건 **"다루기 불편한 벤더 API를 non-blocking · thread-safe로 감싸라"**였고, 그게 **Part 1 = 동시성 / Part 2 = 자료구조**로 갈라지는 구조였다.
> 다음에 같은 유형이 나오면 반사적으로 나오도록, **같은 골격의 문제 10개**를 만들어 두었다.

---

## 1. 이 유형의 정체 — 한 장으로

```
 [벤더 API]  ← 느리다 / 블로킹이다 / thread-safe 아니다 / 값이 안 올 때가 있다
      │
      │  전담 스레드 1개만 이걸 부른다       ← Part 1 의 핵심
      ▼
 [공유 상태]  ── 최신값 스냅샷 ─────────────▶ get_latest()      (동시성 문제)
      │
      └────── 히스토리 / 집계 ──────────────▶ get_at(t), range() (자료구조 문제)
                                               ▲
                     reader 스레드 여러 개 ─────┘  절대 벤더를 기다리지 않는다
```

**출제자가 보는 것**
1. 블로킹을 **한 스레드에 가두는** 발상을 스스로 하는가
2. 공유 상태를 무엇으로 보호하는가 (mutex / atomic / seqlock / 더블버퍼) — **그리고 그 선택을 설명하는가**
3. Part 2에서 메모리를 **숫자로 계산**하고, 조회 비용을 O(log n)/O(1)로 만드는가
4. 경계 조건을 **먼저** 말하는가 (미래 / 범위 밖 / 첫 샘플 이전 / 빈 상태)
5. 에러·시계 역행·종료를 다루는가

## 2. 어디서 연습하나

```
job_interview_prep_research/concurrency_prac/fromVerkadaSrEmbdLinuxEng/
  01_gps_fix_cache/            02_modem_rssi_window/     03_event_tailer_shutdown/
  04_temp_single_flight/       05_frame_latest_and_replay/
  06_config_publish_rollback/  07_badge_audit_dedupe/    08_battery_energy_pipeline/
  09_wifi_scan_snapshot/       10_heartbeat_watchdog/
```

문제마다 원본과 **똑같은 구성**이다.

| 파일 | 무엇 |
|---|---|
| `question_note` | 지문 (배경 · 벤더 API · Part 1/2 · 면접관이 듣고 싶어하는 10가지) |
| `<vendor>.h` | 주어지는 벤더 헤더 |
| `<api>.h` | 내가 구현할 API |
| `<api>.c` | **여기에 내가 코드를 쓴다** (TODO만 있는 stub) |
| `<api>_solution.c` | 모범답안 — 다 풀기 전에는 열지 않는다 |
| `main.c`, `main.sh` | 하네스. `./main.sh`(내 코드) / `./main.sh sol`(모범답안) |

## 3. 드릴 방법 (문제 1개 = 60~75분)

- [ ] **0~5분**: `question_note`만 읽고, 맨 아래 "10가지"에 **종이로** 답을 적는다. 코드 금지.
- [ ] **5~30분**: Part 1만 구현 → `./main.sh`. Part 1 테스트가 PASS할 때까지.
- [ ] **30~55분**: Part 2 구현 → `./main.sh`로 전부 PASS.
- [ ] **55~65분**: `<api>_solution.c`와 **비교**. 다른 점을 3줄로 적는다.
- [ ] **65~75분**: 내가 짠 것을 **소리 내어 설명**한다 (왜 mutex인지, 메모리가 왜 그만큼인지, 경계 조건은 무엇인지).

**막혔을 때만** 이 순서로 본다: `question_note`의 10가지 → [ALS 쉬운 설명](2026-09-25_als_easy_explainer.html) → 모범답안.

## 4. 순서와 일정 (2주 계획)

> 앞쪽 3개가 가장 중요하다. **01 → 02 → 03**을 먼저, 나머지는 약한 쪽부터.

| 순서 | 문제 | Part 1에서 배우는 것 | Part 2에서 배우는 것 |
|---|---|---|---|
| 1 | `01_gps_fix_cache` | 구조체(8바이트 초과) 스냅샷 — mutex vs **seqlock** | 링버퍼 + 이진 탐색 + **prefix sum** |
| 2 | `02_modem_rssi_window` | 값+타임스탬프 묶어 publish, **staleness** 판정 | **monotonic deque**로 슬라이딩 윈도우 min/max |
| 3 | `03_event_tailer_shutdown` | **타임아웃 없는 블로킹 호출에서 스레드 깨우기**, drop 정책 | **시간 버킷 링**으로 O(버킷) 카운트 |
| 4 | `04_temp_single_flight` | **요청 합치기(single-flight)** — condvar + in-flight 플래그 | 버킷 min/max/avg로 범위 질의 |
| 5 | `05_frame_latest_and_replay` | **더블/트리플 버퍼 + refcount**(빌려준 프레임) | 메모리 예산 있는 링 + 이진 탐색 |
| 6 | `06_config_publish_rollback` | **포인터 교체 + refcount**, load-then-incref 함정 | 버전 링 + 롤백, 참조 중 eviction |
| 7 | `07_badge_audit_dedupe` | **MPSC** 4생산자, 도어별 순서 보존, 종료 | **오픈 어드레싱 해시** + 시간 만료, 도어별 링 |
| 8 | `08_battery_energy_pipeline` | 2단 파이프라인 + **timed wait** + 토큰 버킷 | **사다리꼴 적분 prefix sum**, 부분 구간 보간 |
| 9 | `09_wifi_scan_snapshot` | **가변 길이 결과** 스냅샷 (슬롯 + 핀) | 해시맵 + **size-k min-heap** top-k, EWMA |
| 10 | `10_heartbeat_watchdog` | **다수 writer / 단일 reader**, wait-free 저장, false sharing | **lazy update min-heap**(타이머 휠 비교) |

**일정 예시** (하루 1문제, 주말에 2개)
- 1주차: 01, 02, 03, 04, 05
- 2주차: 06, 07, 08, 09, 10 + 09-25 원본 다시 풀기(아무것도 안 보고)

## 5. 어떤 문제가 나와도 쓰는 뼈대 (외울 것)

```c
/* 1) 상태 */
static _Atomic uint32_t g_latest_bits;      /* 또는 mutex + struct / seqlock */
static struct { pthread_mutex_t m; Sample buf[CAP]; uint32_t head, tail; } g_hist;
static _Atomic bool g_running;  static pthread_t g_thr;

/* 2) 전담 스레드 — 벤더를 부르는 유일한 곳 */
static void *sampler(void *_) {
    while (atomic_load_explicit(&g_running, memory_order_acquire)) {
        R r = vendor_blocking_call();
        switch (r.status) {
        case VALID:     publish_latest(r); hist_append(r); break;
        case NO_CHANGE: break;                  /* 이전 값이 여전히 정답 */
        default:        count_error(); backoff(); break;   /* 값은 쓰레기 */
        }
    }
    return NULL;
}

/* 3) init: 첫 호출이 안 막히면 여기서 미리 한 번 → reader가 바로 값을 본다 */
/* 4) deinit: flag=false → (필요하면 vendor_wake()) → join → 그 다음에 해제 */
/* 5) getter: 벤더를 절대 안 부른다. 스냅샷 읽기 / 히스토리는 이진 탐색 */
```

**말로 할 문장 4개**
1. "The blocking call lives in exactly one thread; readers only see what it publishes."
2. "Which kind of non-blocking do you want — no sensor wait, a short lock, or fully wait-free?"
3. "10 minutes at N Hz is M samples × S bytes = K KB, so a fixed ring is fine — no malloc."
4. "Let me state the edge cases first: future, older than the window, before the first sample, empty."

## 6. 이번에 놓친 것 (다음엔 여기서 점수)

- [ ] `NO_CHANGE`의 의미를 **"이전 값이 여전히 유효"**로 읽기
- [ ] 첫 호출이 블로킹하지 않는다는 힌트를 **init에서 활용**하기
- [ ] float 한 개도 **C 표준에서는 원자적이지 않다** → `_Atomic uint32_t`에 비트로
- [ ] Part 2의 **창 경계 샘플은 지우지 않는다** (다음 것도 창 밖일 때만 버린다)
- [ ] 메모리를 **숫자로** 말하기 (샘플레이트 × 시간 × 크기)
- [ ] 경계 조건 4종을 **묻기 전에** 먼저 말하기

## 7. 관련 문서

| 문서 | 언제 |
|---|---|
| [ALS 문제 쉬운 설명](2026-09-25_als_easy_explainer.html) | 원본 문제를 다시 이해할 때 |
| `solutions_with_opus.md` | 원본의 상세 판본(640줄), follow-up 대비 |
| [스터디 노트 23편](../../concurrency_prac/fromVerkadaSrEmbdLinuxEng/notes/index.html) | **개념이 부족할 때 — 여기부터** |
| [연습 문제 10개 인덱스](../../concurrency_prac/fromVerkadaSrEmbdLinuxEng/index.html) | 드릴할 때 |
| [면접 준비 전체 목차](../index.html) | 개념·노트·문제 은행 |
