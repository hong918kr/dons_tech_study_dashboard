# D1. 만능 레시피 — 블로킹 벤더 API를 감싸는 7단계

> **이 노트를 읽고 나면**
> - 처음 보는 벤더 헤더를 받아도 **7단계를 순서대로** 밟아 설계를 말로 끝낼 수 있다
> - **mutex / atomic / seqlock / 슬롯+refcount / 포인터 swap** 중 하나를 근거와 함께 고른다
> - 빈 템플릿을 복사해 **45분 안에** Part 1 + Part 2를 채우는 시간 배분을 갖는다
>
> **선행**: [B4. mutex](B4_mutex.md), [B5. condvar와 바운드 큐](B5_condvar_and_bounded_queue.md), [B6. atomic과 메모리 오더](B6_atomics_and_memory_order.md), [C1. 링 버퍼](C1_ring_buffer.md)
> **이 개념을 쓰는 문제**: 열 문제 전부. `01_gps_fix_cache` ~ `10_heartbeat_watchdog`, 그리고 원본 [Verkada 9/25 ALS 문제](../../../job_interview_prep_research/verkada_sr_embedded_linux_engineer_connectivity/verkada_sep_25_2026_1st_screening_questions/question_note)

---

## 1. 왜 이게 필요한가

이 폴더의 문제 열 개는 지문이 다 다르게 생겼다. GPS, 모뎀, 출입 카드, 배터리, Wi-Fi, 워치독.
그런데 **골격은 하나**다.

> 블로킹하고, thread-safe하지 않고, 특수 반환값이 섞여 있는 벤더 함수가 하나 주어진다.
> 이걸 **non-blocking · thread-safe API**로 감싼다. Part 1은 동시성, Part 2는 히스토리 질의.

문제를 매번 처음부터 생각하면 45분에 못 끝난다. 2026-09-25 스크리닝에서 막힌 지점이
"무엇을 어떤 순서로 결정해야 하는가"였다. 그래서 이 노트는 **개념이 아니라 절차**다.
절차가 있으면 첫 3분에 할 말이 생기고, 동기화 수단을 찍는 게 아니라 **고른 이유**를 말할 수 있고,
시간이 부족해도 어디를 잘라낼지 안다.

---

## 2. 그림으로 먼저

7단계는 하나의 파이프라인을 왼쪽에서 오른쪽으로 짓는 일이다.

```svg
<svg viewBox="0 0 760 250" role="img" aria-label="벤더 API 감싸기 7단계 파이프라인">
  <rect class="box" x="10" y="80" width="110" height="58" rx="8"/><text x="65" y="104" text-anchor="middle">벤더 함수</text><text class="lbl" x="65" y="122" text-anchor="middle">블로킹 · 1스레드</text><rect class="fill-soft" x="175" y="80" width="120" height="58" rx="8"/><text x="235" y="104" text-anchor="middle">전담 스레드</text><text class="lbl" x="235" y="122" text-anchor="middle">③ 스레드 모델</text>
  <rect class="fill-soft" x="350" y="80" width="130" height="58" rx="8"/><text x="415" y="104" text-anchor="middle">공유 상태</text><text class="lbl" x="415" y="122" text-anchor="middle">④ 최신값 publish</text><rect class="box" x="350" y="170" width="130" height="58" rx="8"/><text x="415" y="194" text-anchor="middle">히스토리</text><text class="lbl" x="415" y="212" text-anchor="middle">⑤ 메모리 예산</text>
  <rect class="box" x="545" y="80" width="130" height="58" rx="8"/><text x="610" y="104" text-anchor="middle">독자 N명</text><text class="lbl" x="610" y="122" text-anchor="middle">getter · 대기 없음</text><rect class="box" x="545" y="170" width="130" height="58" rx="8"/><text x="610" y="194" text-anchor="middle">범위 질의</text><text class="lbl" x="610" y="212" text-anchor="middle">O(log n) 또는 O(버킷)</text>
  <line class="accent" x1="120" y1="109" x2="168" y2="109"/><polygon class="accent" points="168,105 176,109 168,113"/><line class="accent" x1="295" y1="109" x2="343" y2="109"/><polygon class="accent" points="343,105 351,109 343,113"/>
  <line class="accent" x1="480" y1="109" x2="538" y2="109"/><polygon class="accent" points="538,105 546,109 538,113"/><line class="muted" x1="415" y1="138" x2="415" y2="166"/><polygon class="muted" points="411,166 415,174 419,166"/>
  <line class="accent" x1="480" y1="199" x2="538" y2="199"/><polygon class="accent" points="538,195 546,199 538,203"/><line class="dash" x1="235" y1="60" x2="235" y2="78"/>
  <text class="lbl" x="235" y="52" text-anchor="middle">① 헤더 읽기  ② 요구사항 질문</text><text class="lbl" x="610" y="52" text-anchor="middle">⑥ 경계·에러·종료   ⑦ 테스트</text>
</svg>
```

스레드 모델은 거의 항상 이 모양이다. 화살표 하나가 벤더를 향해 있고, 나머지는 공유 상태만 본다.

```
              벤더 함수 (여기에 들어갈 수 있는 스레드는 1개)
                    ^
                    | 블로킹 100 ms
              [ 전담 샘플러 스레드 ]  ---- publish ---->  [ 공유 상태 ]
                                                              ^ ^ ^ ^
                                          getter (대기 없음)  A B C D  독자 N명

  잘못된 그림:      독자 A --직접 호출--> 벤더        (100 ms 멈춤 + 동시 진입 UB)
```

---

## 3. 개념 (7단계를 하나씩)

### 3.1 ① 벤더 API 읽기 체크리스트

지문에서 **반드시 뽑아내야 하는 다섯 줄**이다. 못 찾았으면 아직 읽은 게 아니다.

| 확인할 것 | 헤더에서 찾는 말 | 실제 예시 |
| --- | --- | --- |
| 무엇이 블로킹인가, 얼마나 | "blocks", "0..100 ms", "no timeout" | `03`의 `evsrc_read_blocking()`은 **타임아웃이 아예 없다** — 조용한 밤에는 몇 분 |
| thread-safe인가 | "not thread-safe", "static state", "exactly one" | `07`의 `reader_wait_badge()`는 **문별로는 안전**, 같은 문에 둘은 UB → 생산자 4개 |
| 특수 반환값의 의미 | 상태 enum, `-1`, `-2` | `02`의 `MODEM_BUSY`는 "새 값 없음, 이전 값 유효", `MODEM_ERROR`는 "payload 쓰레기" |
| 첫 호출 특례 | "the FIRST call returns immediately" | `01` `02` `08` `09` 전부 → **init에서 호출자 스레드로 한 번 당겨올 수 있다** |
| 에러 시 필드 상태 | "untouched" / "garbage" / "unspecified" | `09`는 BUSY일 때 `out`과 `*found`를 **읽으면 안 된다**, `08`은 STALE이면 미기록 |

두 개 더. **탈출 장치**가 있는가(`03`의 `evsrc_wake()`, `07`의 `reader_cancel()`) — 있으면 종료
설계가 끝난다. 그리고 **시계**가 단조인가 — 이 폴더는 전부 `gettimeofday` 기반이라 뒤로 점프한다.
한 문장으로 말한다: *"The header says it keeps static state, so exactly one thread may be inside it.
That single sentence decides my whole thread model."*

### 3.2 ② 요구사항 질문 3~5개

코딩 전에 묻는다. 묻는 것 자체가 평가 항목(커뮤니케이션)이고, 답을 들으면 설계가 반으로 줄어든다.
영어로 그대로 쓸 문장:

- *"How many threads may be inside the vendor call? The header says static state, so I'll assume exactly one — is that right?"*
- *"Do readers need only the latest value, or the history too? And does 'non-blocking' mean 'never waits for the device', or 'never takes a lock at all'?"*
- *"What is the worst-case sample rate, and how long a window do you want? I need both numbers to size the buffer."*
- *"When the vendor errors, should the previous value stay readable, or should the reader see 'no data'?"*

답을 못 들으면 **가정으로 선언하고 진행**한다. 침묵하지 말고 소리 내어:
*"I'll assume one sampler thread and a 10-minute window at up to 100 Hz. Stop me if that's wrong."*

### 3.3 ③ 스레드 모델 정하기

기본값은 **전담 스레드 정확히 1개**다. 이유가 두 개이고, 두 개를 다 말해야 한다.

첫째, **벤더가 강제한다.** static 상태를 쓰므로 두 스레드가 들어가면 undefined behavior다.
`04_temp_single_flight`의 가짜 프로브는 동시 진입을 감지해 그 자리에서 런을 실패시킨다.
둘째, **지연이 강제한다.** 독자가 직접 호출하면 매 호출이 벤더 블로킹 시간만큼 걸린다.
`01`은 100 ms, `05`는 25 ms, `04`는 실제 600 ms다. 독자가 열두 개면 열두 개가 다 멈춘다.

예외는 세 가지뿐이다.
- **벤더가 자원별로 분리** → 자원 수만큼. `07`은 문 4개라 생산자 4개, 소비자는 `audit_store_write()`가 단일 커서라 1개(MPSC).
- **단계가 서로 속도가 다르다** → 파이프라인. `08`은 샘플러(18/s)와 업로더(10/s)를 분리하고 사이에 바운드 큐를 둔다.
- **전담 스레드가 필요 없다** → `04`는 값을 원하는 호출자 중 한 명이 리더가 되어 읽고 나머지는 기다린다(single flight). `10`은 완전히 거꾸로 — 블로킹 벤더가 없고 writer가 많고 reader가 하나다.

### 3.4 ④ 공유 상태 설계 — 동기화 수단 고르기 순서도

레시피의 심장이다. 질문 두 개로 갈린다: **최신값 하나인가 히스토리인가**,
그리고 **payload가 8바이트에 들어가는가**.

```svg
<svg viewBox="0 0 740 470" role="img" aria-label="동기화 수단 결정 순서도">
  <rect class="fill-soft" x="255" y="10" width="230" height="46" rx="8"/><text x="370" y="38" text-anchor="middle">독자가 원하는 것은?</text><rect class="box" x="20" y="100" width="180" height="46" rx="8"/><text x="110" y="128" text-anchor="middle">모든 값을 하나씩</text>
  <rect class="box" x="255" y="100" width="230" height="46" rx="8"/><text x="370" y="128" text-anchor="middle">가장 최신 값 하나</text><rect class="box" x="540" y="100" width="180" height="46" rx="8"/><text x="630" y="128" text-anchor="middle">범위 집계</text>
  <line class="accent" x1="290" y1="56" x2="150" y2="96"/><polygon class="accent" points="146,92 150,100 156,94"/><line class="accent" x1="370" y1="56" x2="370" y2="96"/><polygon class="accent" points="366,96 370,104 374,96"/>
  <line class="accent" x1="450" y1="56" x2="590" y2="96"/><polygon class="accent" points="584,94 590,100 594,92"/><rect class="fill-soft" x="20" y="190" width="180" height="58" rx="8"/><text x="110" y="214" text-anchor="middle">바운드 큐 + condvar</text><text class="lbl" x="110" y="234" text-anchor="middle">B5 · 03, 07, 08</text>
  <rect class="fill-soft" x="540" y="190" width="180" height="58" rx="8"/><text x="630" y="214" text-anchor="middle">C3 / C4 / C5 로</text><text class="lbl" x="630" y="234" text-anchor="middle">누적합 · 덱 · 버킷</text><line class="accent" x1="110" y1="146" x2="110" y2="186"/><polygon class="accent" points="106,186 110,194 114,186"/>
  <line class="accent" x1="630" y1="146" x2="630" y2="186"/><polygon class="accent" points="626,186 630,194 634,186"/><rect class="box" x="235" y="190" width="270" height="46" rx="8"/><text x="370" y="218" text-anchor="middle">payload가 8바이트 안에 들어가나?</text>
  <line class="accent" x1="370" y1="146" x2="370" y2="186"/><polygon class="accent" points="366,186 370,194 374,186"/><rect class="fill-soft" x="215" y="280" width="150" height="58" rx="8"/><text x="290" y="304" text-anchor="middle">단일 atomic</text><text class="lbl" x="290" y="324" text-anchor="middle">B6 · 02, 10</text>
  <line class="accent" x1="330" y1="236" x2="300" y2="276"/><polygon class="accent" points="296,270 300,280 306,272"/><text class="lbl" x="300" y="258">예</text><rect class="box" x="400" y="280" width="300" height="46" rx="8"/><text x="550" y="308" text-anchor="middle">독자가 포인터를 오래 쥐고 있나?</text>
  <line class="accent" x1="430" y1="236" x2="500" y2="276"/><polygon class="accent" points="494,272 502,280 498,270"/><text class="lbl" x="470" y="258">아니오</text><rect class="fill-soft" x="400" y="370" width="150" height="70" rx="8"/><text x="475" y="394" text-anchor="middle">seqlock</text><text class="lbl" x="475" y="412" text-anchor="middle">또는 mutex 스냅샷</text><text class="lbl" x="475" y="430" text-anchor="middle">B7 / B4 · 01</text>
  <line class="accent" x1="490" y1="326" x2="478" y2="366"/><polygon class="accent" points="474,360 478,370 484,362"/><text class="lbl" x="452" y="348">아니오 (즉시 복사)</text><rect class="fill-soft" x="580" y="370" width="150" height="70" rx="8"/><text x="655" y="394" text-anchor="middle">슬롯 + refcount</text><text class="lbl" x="655" y="412" text-anchor="middle">또는 포인터 swap</text><text class="lbl" x="655" y="430" text-anchor="middle">B7 · 05, 06, 09</text>
  <line class="accent" x1="620" y1="326" x2="650" y2="366"/><polygon class="accent" points="644,362 652,370 654,360"/><text class="lbl" x="690" y="348">예</text>
</svg>
```

| 수단 | 쓰는 조건 | 대가 | 노트 | 문제 |
| --- | --- | --- | --- | --- |
| 단일 atomic (packed) | 값 + 시각이 64비트에 들어간다 | 범위·해상도를 희생. 16비트 dBm + 48비트 us | [B6](B6_atomics_and_memory_order.md) | `02_modem_rssi_window`, `10_heartbeat_watchdog` |
| mutex 스냅샷 | 무엇이든. 기본 정답이고 부끄러운 답이 아니다 | 독자끼리 직렬화. RT 커널에서 우선순위 역전 | [B4](B4_mutex.md) | 전 문제의 v1 |
| seqlock | 읽기가 압도적으로 많고 payload가 커서 복사해 간다 | 쓰기 중 독자는 재시도. reader 기아 가능 | [B7](B7_lockfree_patterns.md) | `01_gps_fix_cache` (40바이트 `struct GpsFix`) |
| 슬롯 N개 + 원자 인덱스 + refcount | 독자가 **포인터를 빌려** 오래 쥔다. 복사 비용이 크다 | 슬롯 메모리 N배, 해제 누락이 곧 누수 | [B7](B7_lockfree_patterns.md) | `05_frame_latest_and_replay`, `09_wifi_scan_snapshot` |
| 불변 객체 + 포인터 swap + refcount | 객체를 매번 새로 만들 수 있고 malloc이 허용된다 | A/B 사이 use-after-free 창을 닫아야 한다 | [B7](B7_lockfree_patterns.md) | `06_config_publish_rollback` |
| 바운드 큐 + condvar | 독자가 **값을 하나도 놓치면 안 된다** | 넘칠 때 정책이 필요. 블로킹 대기 가능 | [B5](B5_condvar_and_bounded_queue.md) | `03`, `07`, `08` |

두 갈래를 같이 쓰기도 한다 — `08`은 최신값을 publish하면서 업로더로 가는 길은 바운드 큐다.

### 3.5 ⑤ 메모리 예산 계산 (C1)

**반드시 소리 내어 숫자를 말한다.** 면접관이 고정으로 묻는 항목이다.

```
용량   = 최악 샘플레이트 × 윈도우 길이
바이트 = 용량 × sizeof(엔트리) + 부가 배열(누적합, 덱, 인덱스)
```

| 문제 | 계산 | 결과 | 넘치면 |
| --- | --- | --- | --- |
| `01_gps_fix_cache` | 10 Hz × 600 s × 40 B + 8 B(누적 거리) | 6000개, 약 290 KB | 오래된 것부터 evict, 단 **윈도우 경계를 덮는 fix 1개는 남긴다** |
| `02_modem_rssi_window` | 100 Hz × 60 s × 12 B + 덱 2개 × 4 B | 6000개, 약 120 KB | tail 삭제 시 덱에서 같은 인덱스도 제거 |
| `03_event_tailer_shutdown` | 버킷 300 × 타입 4 × 4 B | **4.8 KB, 이벤트 레이트와 무관** | 넘칠 일이 없다 — 그게 버킷을 쓰는 이유 |
| `05_frame_latest_and_replay` | 40 fps × 2 s × 4096 B (예산 768 KB) | 320 KB, 통과 | 실제 4K 8 MB면 640 MB → 윈도우 축소 또는 키프레임만 |
| `08_battery_energy_pipeline` | 25 Hz × 1800 s × (8 B ts + 4 B p + 8 B 누적 J) | 45000개, 약 900 KB | evict할 때 누적값 기준점을 함께 옮긴다 |

말할 문장: *"Worst case the device hands me a value every 10 ms, so 10 minutes is 60 000 entries.
At 40 bytes each that's 2.4 MB, which I would not put on this box — so I cap the ring and drop the
oldest, and I say so in the API contract."*

### 3.6 ⑥ 경계·에러·시계·종료 처리 (B8, B9)

**경계.** 코딩 전에 반환 규약을 적는다. 이 폴더는 규약이 전부 같다 — `t`가 미래 → 실패,
윈도우보다 오래됨 → 실패 또는 윈도우 시작으로 clamp(`03`), 첫 샘플 이전 → 실패, `t0 == t1` → 0,
`t0 > t1` → 실패, 범위 안 샘플 0~1개 → 0(보간 없음), 샘플된 적 없음 → "no data".

**에러.** 두 갈래로 나눈다. **즉시 반환하는 에러**(`GPS_ERROR`, `MODEM_ERROR`, `wifi_scan` BUSY,
`evsrc` `-1`)는 그냥 재시도하면 코어를 100% 태운다. **백오프**가 답이고, 이 에러는 이전에
publish한 값을 무효화하지 않는다. **"새 값 없음"**(`MODEM_BUSY`, `BMS_STALE`, ALS의 `NO_CHANGE`)은
payload를 쓰지 않았다. publish도 하지 말고 히스토리에도 넣지 않는다. 이전 값이 계속 유효하다.

**시계.** 전부 `gettimeofday`다. NTP가 뒤로 점프하면 timestamp가 비단조가 되고 이진 탐색의 전제가
깨진다. 방어는 싸다 — **삽입할 때 `ts <= last_ts`면 클램프하거나 버린다.** 말할 문장:
*"I keep the ring monotone by construction, so the binary search stays legal no matter what NTP does."*

**종료.** 벤더 호출 안에 파킹된 스레드를 어떻게 세우나. 순서가 고정이다.

```
1. 락을 잡고  stop = 1  을 세운다        (플래그를 먼저)
2. 같은 락 안에서 broadcast              (timed wait를 깨운다)
3. 락을 놓는다
4. 벤더 탈출 장치를 호출한다              evsrc_wake() / reader_cancel(door)
   ─ 없으면 벤더 호출은 결국 반환한다. 반환 직후 stop을 보고 나간다.
     그래서 deinit 최악 시간 = 벤더 블로킹 1회 + join
5. pthread_join
6. deinit은 idempotent. 두 번 불러도 no-op
7. getter는 deinit 후에도 안전 — 크래시 대신 "no data"
```

순서를 바꾸면 깨진다. 2와 1을 바꾸면 깨어난 스레드가 아직 `stop`을 못 봐서 다시 잔다(lost wakeup).
4를 빼면 `03`처럼 타임아웃 없는 호출에서 영원히 못 나온다. `pthread_cancel`은 쓰지 않는다 —
벤더 static 상태가 찢어진 채 남는다.

### 3.7 ⑦ 테스트 전략

세 층이다. 세 층을 다 말하면 "견고성" 항목이 채워진다.

**1층 — 불변식.** 코드가 참으로 유지해야 하는 문장을 적고 테스트가 그걸 확인한다.
`received == popped + dropped + depth`(`03`), `sampled >= uploaded + dropped`(`08`),
`allocs == frees`(`06`), "존재한 적 없는 값은 나오지 않는다". `01`의 가짜 모듈은
`lon == -(lat + 85.0)`을 항상 만족시키므로 **산술 한 줄로** 찢어짐을 검출한다.

**2층 — brute force 비교.** Part 2의 정답은 O(n) 순진한 구현으로 따로 계산해 비교한다. 이 폴더의
하네스 열 개가 전부 이 방식이다 — 하드코딩된 기대값이 아니라 가짜 벤더가 기록한 ground truth.

**3층 — 도구.** ThreadSanitizer가 race를 잡는다.

```sh
cc -std=c11 -O2 -Wall -Wextra -pthread -fsanitize=thread \
   -o /tmp/t main.c gps_cache_solution.c -lm && /tmp/t
```

면접에서는 실행할 수 없으니 **말로** 한다: *"I'd run this under TSan, and make the seqlock's
payload fields relaxed atomics so it stays clean under the sanitizer."*

### 3.8 45분 타임박스 배분표

시간이 없다고 Part 2를 통째로 버리지 않는다. **구조와 근거를 말하고 코드를 줄인다.**

| 경과 | 하는 일 | 끝났을 때 손에 있는 것 |
| --- | --- | --- |
| 0–3분 | ①② 헤더 읽기, 요구사항 질문 3개, 가정 선언 | API 시그니처 4~5줄 |
| 3–7분 | ③④ 스레드 모델 + 동기화 수단 결정, 이유 한 문장, 그림 1개 | "왜 이걸 골랐나" 문장 |
| 7–20분 | Part 1 구현: `init` / 샘플러 루프 / publish / getter | 컴파일되는 코드 |
| 20–24분 | ⑥ 경계·에러 읊기 + `deinit` 순서 | 종료 경로 |
| 24–28분 | ⑤ Part 2 자료구조 선택 + 메모리 숫자 | 용량과 바이트 수 |
| 28–40분 | Part 2 구현: 삽입 + 질의 + evict | 질의 함수 |
| 40–45분 | ⑦ 테스트 전략 + 개선 여지 + 역질문 | 말로만 |

규칙 셋. **Part 1을 컴파일되는 상태로 먼저 끝낸다**(20분에 안 됐으면 mutex로 내려간다).
**35분에 Part 2가 안 되면** 손을 멈추고 구조와 복잡도를 말로 끝낸다. **마지막 5분은 코딩하지
않는다** — 채점 항목 네 개 중 두 개가 그 5분에 있다.

---

## 4. 코드로 보기

### 4.1 가장 작은 publish — 이게 Part 1의 씨앗이다

```c
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static float    g_val;
static uint64_t g_ts;
static int      g_have;

/* 샘플러 스레드만 호출한다 */
static void publish(float v, uint64_t ts) {
    pthread_mutex_lock(&g_mu);      /* 이 줄이 없으면? 독자가 v와 ts를 */
    g_val = v;                      /* 서로 다른 샘플에서 집어갈 수 있다 */
    g_ts  = ts;
    g_have = 1;
    pthread_mutex_unlock(&g_mu);
}

/* 독자 N명이 호출한다 */
int get_latest(float *v, uint64_t *ts) {
    pthread_mutex_lock(&g_mu);
    int ok = g_have;
    if (ok) { *v = g_val; *ts = g_ts; }   /* 락 안에서 둘을 같이 복사 */
    pthread_mutex_unlock(&g_mu);
    return ok ? 0 : -1;
}
```

임계 구역이 **대입 세 줄**뿐이다. 벤더 호출은 락 밖에 있다. 락을 잡고 `gps_wait_for_fix()`를
부르면 독자가 100 ms 기다리고, 감싼 의미가 사라진다.

다음 단계를 볼 자리: seqlock은 `01_gps_fix_cache/gps_cache_solution.c`, 값+시각 packing은
`02_modem_rssi_window/rssi_window_solution.c`, 바운드 큐와 종료는
`03_event_tailer_shutdown/event_queue_solution.c`, 슬롯+refcount는
`05_frame_latest_and_replay/frame_store_solution.c`, wait-free는
`10_heartbeat_watchdog/watchdog_solution.c`.

### 4.2 빈 코드 템플릿 — 문제를 풀 때 이걸 복사해서 시작한다

주석만 있다. 순서가 곧 레시피다. 위에서 아래로 채우면 Part 1이 끝난다.

```c
/* =====  X_store.c  =====================================================
 * 벤더: <함수 시그니처>  — 블로킹 <N> ms, not thread-safe, 첫 호출 즉시 반환
 * 설계: Part 1 = <mutex 스냅샷 / packed atomic / seqlock / 슬롯+refcount>
 *       이유 = <한 문장>
 *       Part 2 = <링 + 이진탐색 / 누적합 / 덱 / 버킷 / 해시 / 힙>
 *       이유 = <한 문장>
 *       메모리 = <레이트> x <윈도우> x <엔트리 크기> = <바이트>
 * ===================================================================== */

/* ---- 1. 상태 ------------------------------------------------------- */
/* 공유 상태: 최신값 + 그 값의 timestamp (반드시 한 덩어리로 publish)   */
/* 히스토리: 링 버퍼 + head/tail/count (+ 누적합/덱/인덱스 배열)         */
/* 동기화 객체: mutex 1개 (히스토리용), atomic (최신값용), stop 플래그   */
/* 스레드 핸들: pthread_t sampler; int started;                         */
/* 통계: sampled / dropped / errors — 하네스가 읽는다                    */

/* ---- 2. 작은 헬퍼 -------------------------------------------------- */
/* now_us()            — 벤더가 주는 시계를 그대로 쓴다                  */
/* ring_push(...)      — 꽉 차면 tail evict, 부가 배열도 같이 갱신       */
/* lower_bound(t)      — 첫 ts >= t 의 인덱스. 링 인덱스 보정 주의       */
/* upper_bound(t)      — 마지막 ts <= t. 경계에서 한 칸 실수하기 쉽다     */

/* ---- 3. 샘플러 스레드 ---------------------------------------------- */
/* for (;;) {                                                          */
/*   if (stop) break;                  // 락 아래에서 읽는다            */
/*   r = vendor_call(...);              // 락 밖. 여기서 블로킹한다      */
/*   if (r == ERROR)   { backoff(); continue; }   // 스핀 금지           */
/*   if (r == NO_NEW)  { continue; }              // 이전 값 유효        */
/*   publish(sample);                  // 최신값                        */
/*   history_append(sample);           // 히스토리 (락은 여기서만)       */
/* }                                                                   */

/* ---- 4. init ------------------------------------------------------- */
/* mutex/condvar 초기화 → 첫 샘플을 호출자 스레드에서 1회 당겨온다      */
/* (벤더의 "첫 호출은 즉시 반환" 특례. 이때 스레드는 아직 없으니 안전)   */
/* → 스레드 생성 → 실패 시 정리하고 -1                                  */

/* ---- 5. Part 1 getter ---------------------------------------------- */
/* 벤더를 절대 부르지 않는다. 블로킹하지 않는다.                        */
/* 값과 timestamp를 같은 샘플에서 가져온다 (여기가 채점 포인트)          */
/* 아직 샘플 없음 → "no data" 규약대로 반환                              */

/* ---- 6. Part 2 질의 ------------------------------------------------ */
/* 경계 먼저: t 미래 / 윈도우 밖 / 첫 샘플 이전 / t0>t1 / t0==t1 /       */
/*            샘플 0~1개  → 규약대로 즉시 반환                          */
/* 그 다음 본체: 이진 탐색 2회 + 누적합 차이, 또는 버킷 폴드             */
/* 부분 구간 끝단을 정확히 처리했는가? (버킷은 raw로, 적분은 보간으로)   */

/* ---- 7. deinit ----------------------------------------------------- */
/* 락 잡고 stop=1 → broadcast → 락 해제 → 벤더 탈출 장치 → join         */
/* idempotent. 이후 getter는 크래시 대신 "no data"                       */
/* 빌려준 포인터가 있으면 release가 여전히 동작해야 한다                 */

/* ---- 8. 말로 남길 것 ----------------------------------------------- */
/* 고른 이유 / 버린 대안과 조건 / 메모리 숫자 / TSan / 다음에 개선할 것  */
```

---

## 5. 단계별로 만들어 보기

같은 문제를 세 번 푼다. 면접에서도 이 순서로 **말한다** — 진행을 보여 주는 게 점수다.

### v0 — 틀린 버전: getter가 벤더를 부른다

```c
int get_latest(float *v) {
    struct SensorReading r = read_next_sample();   /* 100 ms 블로킹 */
    if (r.status != VALID) return -1;
    *v = r.lux;
    return 0;
}
```

두 가지가 동시에 틀렸다. **호출자가 100 ms 멈춘다**(감싼 의미가 없다), 그리고 **독자 둘이 동시에
부르면 벤더 static 상태가 깨진다**(UB). mutex를 둘러도 두 번째만 고쳐지고, 첫 번째는 더 나빠진다 —
이제 독자들이 줄을 선다.

### v1 — 고친 버전: 전담 스레드 + mutex 스냅샷

샘플러 스레드가 벤더를 독점하고 `publish()`로 최신값을 넣고, getter는 락을 잠깐 잡고 복사만 한다.
§4.1이 바로 이것이다. **이게 정답이다.** v1에서 멈춰도 감점이 아니다 — 근거를 말하면 된다:
*"A mutex here is held for three assignments, tens of nanoseconds — not the thing that will hurt us."*

### v2 — 개선: 제약이 v1을 깨뜨릴 때만 올라간다

| v1이 부족한 이유 | 올라갈 곳 | 문제 |
| --- | --- | --- |
| 독자가 아주 많고 읽기 지연이 규격 | seqlock — 독자가 락을 아예 안 잡는다 | `01` |
| payload가 8바이트 이하 | packed atomic — 한 번의 load로 끝 | `02` |
| 복사 비용이 너무 큼(프레임 8 MB) | 슬롯 + refcount로 빌려주기 | `05`, `09` |
| RT 커널에서 우선순위 역전이 위험 | 락 없는 경로 또는 우선순위 상속 mutex | `01`의 5번 질문 |

---

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
| --- | --- | --- | --- |
| 락을 잡은 채로 벤더 호출 | getter 최악 지연이 벤더 블로킹 시간과 같다. latency 검사 FAIL | 임계 구역이 100 ms짜리가 됐다 | 벤더 호출은 락 밖. 결과가 나온 뒤에만 락을 잡고 복사 |
| 값과 timestamp를 따로 읽음 | 가끔 "그 값은 존재한 적 없다" FAIL. 재현이 안 됨 | 두 번의 load 사이에 publish가 끼어든다 | 하나의 atomic에 packing, 또는 seqlock, 또는 같은 락 안에서 함께 복사 |
| "새 값 없음"을 publish | 쓰레기 lux/lat/amps가 최신값으로 올라간다 | `BUSY`/`STALE`/`NO_CHANGE`는 payload를 쓰지 않는다 | status를 먼저 분기. VALID일 때만 publish/append |
| `stop` 플래그만 세우고 끝 | `deinit`이 몇 초, 또는 영구 hang | 스레드가 타임아웃 없는 벤더 호출 안에 있다 | 탈출 장치를 부른다. 없으면 최악 = 벤더 1회 + join임을 명시 |
| evict할 때 부가 자료구조를 안 고침 | min/max가 사라진 샘플을 가리킨다. 누적합이 음수 거리 | 링의 tail만 움직였다 | evict는 한 함수로 — 링 + 덱 + 누적 기준점 + 카운트를 함께 |
| 윈도우 경계를 덮는 샘플을 evict | `value_at(now - window)`가 갑자기 실패 | 그 시각에 유효한 샘플은 윈도우보다 **오래된** 샘플이다 | "경계를 덮는 한 개는 남긴다"를 규칙으로. `01`의 7번 질문 |

---

## 7. 손으로 확인하기

레시피를 시험하는 방법은 하나다. **타이머를 켜고 템플릿으로 한 문제를 처음부터 푼다.**

```sh
cd 03_event_tailer_shutdown
sed -n '/listening for/,$p' question_note          # 10문항을 먼저 종이에 답한다
./main.sh sol                                     # 정답 동작을 눈으로 본다 (전부 PASS)
./main.sh                                          # 스텁이 지금 몇 개 FAIL인지 기록
```

그리고 §4.2 템플릿을 `event_queue.c` 위에 붙여 놓고 45분 타이머를 켠다. 7분 지점에서 손을 멈추고
"동기화 수단과 이유"가 적혀 있는지 본다. 없으면 레시피를 건너뛴 것이다. 경계와 race만 따로 보려면
`cd 01_gps_fix_cache && ./main.sh window`(1.5초 윈도우로 eviction 경계)와
`cd 09_wifi_scan_snapshot && ./main.sh tsan`.

---

## 8. 자가 점검

```check
Q: 벤더 헤더에서 "not thread-safe: static state"를 봤다. 여기서 바로 따라 나오는 설계 결정 두 개는?
A: 첫째, 그 함수에 들어갈 스레드를 정확히 하나로 못 박는다. 전담 샘플러 스레드가 그것을 독점한다.
   둘째, 독자용 getter는 그 함수를 절대 부르지 않는다. getter는 공유 상태만 읽는다.
   덤으로 init에서 "첫 호출은 즉시 반환" 특례를 쓸 수 있다 — 아직 스레드가 없으므로 호출자 스레드가
   유일한 호출자이고, 규칙을 깨지 않는다.

Q: payload가 8바이트를 넘으면 왜 단일 atomic이 안 되나? `_Atomic struct GpsFix`는 무엇으로 컴파일되나?
A: 하드웨어가 한 번에 원자적으로 store할 수 있는 폭이 보통 8바이트(일부에서 16)다. 40바이트 구조체는
   그 폭을 넘는다. `_Atomic struct GpsFix`는 문법적으로는 되지만 컴파일러가 숨은 락을 붙이므로
   `atomic_is_lock_free()`가 false를 돌려준다. 즉 공짜 atomic이 아니라 감춰진 mutex다.
   그래서 seqlock, 더블/트리플 버퍼, 또는 명시적 mutex 스냅샷으로 간다.

Q: "non-blocking"을 요구했다. mutex를 쓰면 요구사항 위반인가?
A: 아니다. 고객이 신경 쓰는 것은 "독자가 벤더의 100 ms를 기다리지 않는다"다. 대입 세 줄짜리
   임계 구역은 수십 나노초라 문제가 되지 않는다. 다만 RT 커널에서 저우선순위 독자가 락을 쥔 채
   선점되면 샘플러가 그 뒤에서 막힌다(우선순위 역전). 그 시나리오가 요구사항이라면 lock-free 경로로
   올라간다. 면접에서는 이 구분을 먼저 말하고 시작한다.

Q: 시간이 모자라 Part 2를 못 짰다. 남은 5분에 무엇을 말하나?
A: 자료구조 선택과 근거, 메모리 숫자, 각 연산의 복잡도, 그리고 경계 조건 규약. "링 버퍼 + 누적합,
   삽입 O(1) 질의 O(log n), 10분 x 10 Hz x 40바이트 = 약 290 KB, 미래 시각은 -1, 윈도우 밖은 -1,
   경계를 덮는 샘플 하나는 남긴다"까지 말하면 코드가 없어도 평가 항목 네 개 중 세 개가 채워진다.
   코드를 반쯤 짜고 아무 말도 안 하는 것이 최악이다.

Q: `deinit`에서 플래그와 broadcast의 순서가 왜 고정인가?
A: 락을 잡고 stop=1을 쓴 다음, 같은 락 구간 안에서 broadcast한다. 순서를 바꾸면 깨어난 스레드가
   아직 stop을 보지 못한 상태로 predicate를 확인하고 다시 잠들 수 있다(lost wakeup).
   벤더 탈출 장치는 락을 놓은 뒤에 부른다 — 벤더 함수가 얼마나 걸릴지 모르므로 락을 쥔 채로
   부르면 안 된다. 마지막이 join이고, join 전에 상태를 free하면 안 된다.

```

---

## 9. 요약 카드

- **골격은 하나다**: 전담 스레드 1개가 벤더를 독점 → 공유 상태에 publish → 독자는 대기 없이 읽는다.
- **① 헤더에서 다섯 줄**: 블로킹 시간 / thread-safe 여부 / 특수 반환값 / 첫 호출 특례 / 에러 시 필드 상태.
- **② 질문 3개를 영어로 던진다.** 답이 없으면 가정으로 선언하고 진행한다.
- **③ 기본은 전담 스레드 1개.** 예외는 자원별 분리(`07`), 파이프라인(`08`), 스레드 없음(`04`), 거꾸로(`10`).
- **④ 순서도**: 모든 값이 필요하면 큐+condvar → 최신값이면 8바이트 이하는 atomic, 넘으면 seqlock, 빌려주면 슬롯+refcount.
- **⑤ 숫자를 말한다**: 최악 레이트 × 윈도우 × 엔트리 크기, 그리고 넘칠 때 정책.
- **⑥ 경계 규약을 코딩 전에 적는다.** 에러는 백오프, "새 값 없음"은 publish 금지, 시계는 삽입 시 클램프.
- **⑦ 종료 순서**: 락 → `stop=1` → broadcast → 락 해제 → 탈출 장치 → join. idempotent.
- **45분**: 7분에 설계, 20분에 Part 1 컴파일, 40분에 손 놓고 말하기. 마지막 5분은 코딩하지 않는다.
- 문제를 풀 때는 **§4.2의 빈 템플릿**을 복사해서 시작한다. 다음: [D2. 문제 지도](D2_problem_map.md) → [D3. 면접에서 말하는 법](D3_interview_talking.md).
