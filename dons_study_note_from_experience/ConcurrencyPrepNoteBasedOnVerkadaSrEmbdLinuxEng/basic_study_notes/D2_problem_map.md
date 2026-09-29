# D2. 문제 지도 — 어떤 개념이 어디에 쓰이나

> **이 노트를 읽고 나면**
> - 열 문제가 **어떤 개념을 쓰는지** 한 표에서 보고, 내가 약한 개념을 쓰는 문제만 골라 풀 수 있다
> - 시간이 얼마 남았느냐에 따라 **세 가지 학습 경로** 중 하나를 골라 바로 시작할 수 있다
> - 문제를 열기 전에 **무엇을 먼저 읽어야 하는지** 찾아볼 수 있다
>
> **선행**: [D1. 만능 레시피](D1_wrapper_recipe.md)
> **이 개념을 쓰는 문제**: 열 문제 전부 + 원본 [Verkada 9/25 ALS 문제](../../../job_interview_prep_research/verkada_sr_embedded_linux_engineer_connectivity/verkada_sep_25_2026_1st_screening_questions/question_note)

---

## 1. 왜 이게 필요한가

노트가 24편, 문제가 10개다. 순서 없이 읽으면 두 가지가 일어난다.

첫째, **개념 노트를 다 읽고 나서도 문제를 못 푼다.** mutex를 읽었는데 `01_gps_fix_cache`를 열면
40바이트 구조체가 나오고, mutex 노트에 그 얘기가 없었다고 느낀다. 사실은 있었는데 어디에
연결되는지를 몰랐던 것이다.

둘째, **이미 아는 것을 또 읽는다.** `03`과 `07`과 `08`은 셋 다 "바운드 큐 + condvar"다.
세 번 처음부터 공부할 필요가 없다. 대신 `07`은 생산자가 4개라는 점, `08`은 시간 제한 대기가
붙는다는 점만 새로 배우면 된다.

그래서 이 노트는 **연결표**다. 개념 ↔ 문제를 양방향으로 찾는다.

---

## 2. 그림으로 먼저

열 문제의 공통 골격과, 각 분기에서 어느 문제가 어디로 가는지.

```svg
<svg viewBox="0 0 760 400" role="img" aria-label="열 문제의 공통 골격과 분기점">
  <rect class="box" x="15" y="30" width="120" height="52" rx="8"/>
  <text x="75" y="52" text-anchor="middle">벤더 함수</text>
  <text class="lbl" x="75" y="70" text-anchor="middle">블로킹 · 1스레드</text>

  <rect class="fill-soft" x="185" y="30" width="150" height="52" rx="8"/>
  <text x="260" y="52" text-anchor="middle">전담 스레드</text>
  <text class="lbl" x="260" y="70" text-anchor="middle">분기 A</text>

  <rect class="fill-soft" x="390" y="30" width="150" height="52" rx="8"/>
  <text x="465" y="52" text-anchor="middle">공유 상태 publish</text>
  <text class="lbl" x="465" y="70" text-anchor="middle">분기 B · Part 1</text>

  <rect class="fill-soft" x="595" y="30" width="150" height="52" rx="8"/>
  <text x="670" y="52" text-anchor="middle">히스토리 질의</text>
  <text class="lbl" x="670" y="70" text-anchor="middle">분기 C · Part 2</text>

  <line class="accent" x1="135" y1="56" x2="178" y2="56"/>
  <polygon class="accent" points="178,52 186,56 178,60"/>
  <line class="accent" x1="335" y1="56" x2="383" y2="56"/>
  <polygon class="accent" points="383,52 391,56 383,60"/>
  <line class="accent" x1="540" y1="56" x2="588" y2="56"/>
  <polygon class="accent" points="588,52 596,56 588,60"/>

  <line class="muted" x1="260" y1="82" x2="260" y2="110"/>
  <line class="muted" x1="465" y1="82" x2="465" y2="110"/>
  <line class="muted" x1="670" y1="82" x2="670" y2="110"/>

  <text class="lbl" x="185" y="128">전담 1개 — 01 02 03 05 06 09</text>
  <text class="lbl" x="185" y="148">자원별 N개 — 07 (문 4개)</text>
  <text class="lbl" x="185" y="168">2단 파이프라인 — 08</text>
  <text class="lbl" x="185" y="188">스레드 없음 — 04</text>
  <text class="lbl" x="185" y="208">거꾸로(writer 다수) — 10</text>

  <text class="lbl" x="390" y="128">packed atomic — 02 10</text>
  <text class="lbl" x="390" y="148">seqlock — 01</text>
  <text class="lbl" x="390" y="168">슬롯+refcount — 05 09</text>
  <text class="lbl" x="390" y="188">포인터 swap — 06</text>
  <text class="lbl" x="390" y="208">큐+condvar — 03 07 08</text>
  <text class="lbl" x="390" y="228">single flight — 04</text>

  <text class="lbl" x="595" y="128">링+이진탐색 — 01 05</text>
  <text class="lbl" x="595" y="148">누적합 — 01 08</text>
  <text class="lbl" x="595" y="168">단조 덱 — 02</text>
  <text class="lbl" x="595" y="188">시간 버킷 — 03 04</text>
  <text class="lbl" x="595" y="208">해시맵 — 07 09</text>
  <text class="lbl" x="595" y="228">힙 / top-k — 09 10</text>

  <rect class="box" x="15" y="290" width="730" height="90" rx="8"/>
  <text x="380" y="316" text-anchor="middle">모든 문제에 공통으로 채점되는 것</text>
  <text class="lbl" x="380" y="340" text-anchor="middle">init 직후 값이 있다 · getter가 벤더 블로킹 시간의 1/10 이하 · 찢어진 값 없음</text>
  <text class="lbl" x="380" y="360" text-anchor="middle">경계 조건(미래/윈도우 밖/첫 샘플 이전/역순 범위) · deinit이 빠르고 idempotent</text>
</svg>
```

Part 1 기법은 "얼마나 큰 값을, 얼마나 오래 쥐는가"라는 하나의 축에 늘어선다.

```
payload 크기 →   4~8 B          40 B          1.6 KB         4 KB       동적 객체
                   |              |              |             |            |
기법            packed atomic   seqlock    슬롯+refcount   슬롯+refcount  포인터 swap
                   02, 10         01            09             05            06
                   |              |              |             |            |
독자가 쥐는 법   복사(즉시)     복사(즉시)     복사 또는 핀   포인터 대여   포인터 대여
                                                                |            |
                                                        해제 누락 = 프레임 드롭 / 누수

  ─── 위 축과 무관한 세 문제 ───
  03 07 08   값을 하나도 놓치면 안 된다  →  바운드 큐 + condvar (드롭 정책이 설계다)
  04         배경 스레드가 없다          →  호출자 중 리더 1명 (single flight)
  10         writer가 많고 reader가 1명  →  wait-free 저장 + 조건부 kick
```

---

## 3. 개념 ↔ 문제 매트릭스

### 3.1 표 읽는 법

- **●** = 핵심. 이 개념을 모르면 그 문제의 해당 Part를 **못 푼다**.
- **○** = 조연. 쓰이기는 하지만 몰라도 우회할 수 있거나, 다른 문제에서 이미 배운 것이다.
- 빈 칸 = 쓰이지 않는다.
- A 트랙([A1](A1_c_refresher.md) ~ [A4](A4_linux_userspace_basics.md))은 **모든 문제의 전제**라
  행마다 표시하지 않았다. C 문법·메모리·빌드 도구·POSIX가 흔들리면 A부터 읽는다.

### 3.2 동시성 매트릭스 (Part 1)

열 머리글은 [B1](B1_threads_and_scheduler.md) 스레드/스케줄러, [B2](B2_pthread_api.md) pthread API,
[B3](B3_race_and_critical_section.md) race/임계구역, [B4](B4_mutex.md) mutex,
[B5](B5_condvar_and_bounded_queue.md) condvar/바운드 큐, [B6](B6_atomics_and_memory_order.md) atomic/메모리 오더,
[B7](B7_lockfree_patterns.md) lock-free 패턴, [B8](B8_time_and_timeouts.md) 시간/타임아웃,
[B9](B9_lifecycle_and_shutdown.md) 수명/종료.

| 문제 | B1 | B2 | B3 | B4 | B5 | B6 | B7 | B8 | B9 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| [01_gps_fix_cache](../01_gps_fix_cache/question_note) | ○ | ● | ● | ○ | ○ | ● | ● | ○ | ● |
| [02_modem_rssi_window](../02_modem_rssi_window/question_note) | ○ | ● | ● | ○ | ○ | ● | ○ | ● | ● |
| [03_event_tailer_shutdown](../03_event_tailer_shutdown/question_note) | ○ | ● | ● | ● | ● | ○ |  | ● | ● |
| [04_temp_single_flight](../04_temp_single_flight/question_note) | ○ | ● | ● | ● | ● |  |  | ● | ● |
| [05_frame_latest_and_replay](../05_frame_latest_and_replay/question_note) | ○ | ● | ● | ○ |  | ● | ● | ○ | ● |
| [06_config_publish_rollback](../06_config_publish_rollback/question_note) | ○ | ● | ● | ● |  | ● | ● | ○ | ● |
| [07_badge_audit_dedupe](../07_badge_audit_dedupe/question_note) | ● | ● | ● | ● | ● | ○ |  | ● | ● |
| [08_battery_energy_pipeline](../08_battery_energy_pipeline/question_note) | ● | ● | ● | ● | ● | ○ |  | ● | ● |
| [09_wifi_scan_snapshot](../09_wifi_scan_snapshot/question_note) | ○ | ● | ● | ○ |  | ● | ● | ○ | ● |
| [10_heartbeat_watchdog](../10_heartbeat_watchdog/question_note) | ● | ● | ● | ○ | ● | ● | ● | ● | ● |

읽는 법 두 가지. **행으로** 읽으면 그 문제를 풀기 전에 필요한 노트가 나온다.
**열로** 읽으면 그 개념을 연습할 문제가 나온다 — 예를 들어 B7(lock-free)이 ●인 문제는
`01`, `05`, `06`, `09`, `10` 다섯 개고, 그게 곧 lock-free 연습 코스다.

### 3.3 자료구조 매트릭스 (Part 2)

열 머리글은 [C1](C1_ring_buffer.md) 링 버퍼, [C2](C2_sorted_series_and_binary_search.md) 정렬 계열/이진탐색,
[C3](C3_prefix_sum_and_range_query.md) 누적합/범위 질의, [C4](C4_sliding_window_and_monotonic_deque.md) 슬라이딩 윈도우/단조 덱,
[C5](C5_time_buckets_and_histogram.md) 시간 버킷/히스토그램, [C6](C6_hashmap_open_addressing.md) 해시맵/오픈 어드레싱,
[C7](C7_heap_and_topk.md) 힙/top-k.

| 문제 | C1 | C2 | C3 | C4 | C5 | C6 | C7 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| [01_gps_fix_cache](../01_gps_fix_cache/question_note) | ● | ● | ● |  |  |  |  |
| [02_modem_rssi_window](../02_modem_rssi_window/question_note) | ● | ○ | ○ | ● |  |  |  |
| [03_event_tailer_shutdown](../03_event_tailer_shutdown/question_note) | ● |  | ○ |  | ● |  |  |
| [04_temp_single_flight](../04_temp_single_flight/question_note) | ● | ○ | ○ |  | ● |  |  |
| [05_frame_latest_and_replay](../05_frame_latest_and_replay/question_note) | ● | ● |  |  |  |  |  |
| [06_config_publish_rollback](../06_config_publish_rollback/question_note) | ● | ○ |  |  |  |  |  |
| [07_badge_audit_dedupe](../07_badge_audit_dedupe/question_note) | ● |  |  |  | ○ | ● |  |
| [08_battery_energy_pipeline](../08_battery_energy_pipeline/question_note) | ● | ● | ● |  |  |  |  |
| [09_wifi_scan_snapshot](../09_wifi_scan_snapshot/question_note) | ● |  |  |  | ○ | ● | ● |
| [10_heartbeat_watchdog](../10_heartbeat_watchdog/question_note) |  |  |  |  |  | ○ | ● |

**C1이 거의 전부 ●인 이유**가 이 표의 가장 중요한 정보다. 링 버퍼는 고정 메모리로 최근
윈도우를 담는 유일한 기본 도구라서, Part 2의 첫 문장은 거의 항상 "링 버퍼를 두고"로 시작한다.
링 버퍼가 손에 안 붙어 있으면 다른 어떤 것도 안 된다. 그래서 [C1](C1_ring_buffer.md)이 C 트랙의 1번이다.

### 3.4 문제별 한 줄 요약

| # | 문제 | 한 줄 요약 | Part 1 동시성 기법 | Part 2 자료구조 | 난이도 | 예상 시간 |
| --- | --- | --- | --- | --- | --- | --- |
| 01 | [gps_fix_cache](../01_gps_fix_cache/question_note) | 40바이트 GPS fix를 찢어지지 않게 publish하고, 10분간 이동 거리를 O(log n)에 | seqlock (odd/even + 두 개의 fence) | 링 + 이진탐색 + 거리 누적합 | 상 | 60분 |
| 02 | [modem_rssi_window](../02_modem_rssi_window/question_note) | RSSI 값과 **나이**를 한 덩어리로 주고, 윈도우 min/max를 상시 준비 | 값+시각을 64비트 하나에 packing | 링 + 단조 덱 2개 + 러닝 합 | 중 | 50분 |
| 03 | [event_tailer_shutdown](../03_event_tailer_shutdown/question_note) | **타임아웃 없는** 블로킹 읽기를 큐로 감싸고 깨끗하게 종료 | 바운드 큐 + condvar, drop-oldest | 시간 버킷 링 300×1s, lazy advance | 중 | 45분 |
| 04 | [temp_single_flight](../04_temp_single_flight/question_note) | 8스레드가 동시에 요청해도 I2C 읽기는 **딱 한 번** | single flight: mutex + condvar + inflight + generation | 60×1분 버킷(min/max/sum/cnt) + raw 링 | 중상 | 50분 |
| 05 | [frame_latest_and_replay](../05_frame_latest_and_replay/question_note) | 프레임을 **복사 없이** 빌려주고, 느린 독자가 writer를 못 막게 | 트리플 버퍼 + refcount (포인터 대여) | 메모리 예산 링 + 이진탐색 | 상 | 60분 |
| 06 | [config_publish_rollback](../06_config_publish_rollback/question_note) | 불변 config를 포인터 교체로 publish, 버전 롤백, use-after-free 창 닫기 | 포인터 swap + refcount | 버전 링 8개 + 롤백 | 상 | 65분 |
| 07 | [badge_audit_dedupe](../07_badge_audit_dedupe/question_note) | 문 4개 → 감사 로그 1개, **문별 순서 보존**, 중복 배지 억제 | MPSC 바운드 큐, condvar 2개, 유한 대기 후 drop | 오픈 어드레싱 해시 1024 + 시간 만료 + 문별 링 | 상 | 70분 |
| 08 | [battery_energy_pipeline](../08_battery_energy_pipeline/question_note) | 샘플러 18/s vs 업로더 10/s, 역압 흡수, 임의 구간 에너지 적분 | 2단 파이프라인 + 토큰 버킷(timed wait) | 링 + 사다리꼴 누적합 + 끝단 보간 | 최상 | 75분 |
| 09 | [wifi_scan_snapshot](../09_wifi_scan_snapshot/question_note) | **가변 길이** 스캔 결과(1.6 KB)를 스냅샷으로 publish, AP 순위 | 3슬롯 + 원자 인덱스 + refcount | BSSID 해시맵 256 + size-k min-heap + EWMA | 상 | 70분 |
| 10 | [heartbeat_watchdog](../10_heartbeat_watchdog/question_note) | writer 다수 / reader 1명. heartbeat은 wait-free, kick은 **전원 건강할 때만** | 패딩된 슬롯에 relaxed/release store | lazy update min-heap + id→인덱스 배열 | 중상 | 55분 |

"예상 시간"은 **처음 풀 때** 기준이고 개념 노트를 읽은 상태를 가정한다. 두 번째는 절반이면 된다.
실제 면접은 45분에 Part 1 + Part 2다. 그 속도가 목표이고, 처음부터 그 속도가 나오지는 않는다.

### 3.5 원본 Verkada 문제는 이 지도의 어디인가

2026-09-25 1차 스크리닝 문제(ALS 조도 센서, `read_next_sample()`)를 같은 표에 넣으면:

| 항목 | 원본 ALS 문제 | 이 지도에서 |
| --- | --- | --- |
| 벤더 API | `struct SensorReading read_next_sample()` — 최대 1초 블로킹, `NO_CHANGE`/`ERROR`, 첫 호출 즉시 | `01`, `02`와 같은 모양 |
| Part 1 | 최신 lux `float` 하나 — mutex 또는 atomic이면 충분 | B4 ● / B6 ● — payload가 4바이트라 **가장 쉬운 갈래** |
| Part 2 | `get_lux_at(t)` = ts ≤ t인 가장 최근 VALID 값 | C1 ● + C2 ● — 링 버퍼 + 이진 탐색 |
| 면접관 힌트 | circular queue → linked list → **red-black tree** 의 진행 | C2의 확장. 이 폴더에는 rbtree 문제가 없고, 원본 폴더의 `recent_lux_solution_rbtree.c`가 그 답 |
| 10문항 | 동시성 5 + 자료구조 3 + 에러/수명 2 | 열 문제의 `question_note` 하단이 전부 이 비율 |

즉 **원본은 이 지도의 왼쪽 아래 구석**이다. `01_gps_fix_cache`는 원본에서 payload를 4바이트에서
40바이트로 키우고 Part 2에 누적합을 얹은 것이고, `02_modem_rssi_window`는 원본에 staleness와
슬라이딩 min/max를 얹은 것이다. 원본을 풀 수 있으면 `01`과 `02`의 절반은 이미 푼 셈이다.

거꾸로 말하면 — **원본이 쉬웠는데도 막혔다면** 부족한 것은 알고리즘이 아니라 §2의 골격 자체다.
[D1의 7단계](D1_wrapper_recipe.md)를 먼저 외우는 것이 어떤 문제를 푸는 것보다 빠르다.

### 3.6 문제를 풀기 전에 읽어야 할 노트

각 행의 노트만 읽으면 그 문제를 풀 수 있다. "복습"은 이미 다른 문제에서 나온 것이다.

| 문제 | 반드시 먼저 | 있으면 좋음 |
| --- | --- | --- |
| `01_gps_fix_cache` | [B3](B3_race_and_critical_section.md) · [B6](B6_atomics_and_memory_order.md) · [B7](B7_lockfree_patterns.md) · [C1](C1_ring_buffer.md) · [C2](C2_sorted_series_and_binary_search.md) · [C3](C3_prefix_sum_and_range_query.md) | [B9](B9_lifecycle_and_shutdown.md) |
| `02_modem_rssi_window` | [B6](B6_atomics_and_memory_order.md) · [B8](B8_time_and_timeouts.md) · [C1](C1_ring_buffer.md) · [C4](C4_sliding_window_and_monotonic_deque.md) | [A2](A2_memory_and_bits.md) (packing) |
| `03_event_tailer_shutdown` | [B4](B4_mutex.md) · [B5](B5_condvar_and_bounded_queue.md) · [B9](B9_lifecycle_and_shutdown.md) · [C5](C5_time_buckets_and_histogram.md) | [B8](B8_time_and_timeouts.md) |
| `04_temp_single_flight` | [B4](B4_mutex.md) · [B5](B5_condvar_and_bounded_queue.md) · [B8](B8_time_and_timeouts.md) · [C5](C5_time_buckets_and_histogram.md) | [C1](C1_ring_buffer.md) |
| `05_frame_latest_and_replay` | [B6](B6_atomics_and_memory_order.md) · [B7](B7_lockfree_patterns.md) · [C1](C1_ring_buffer.md) · [C2](C2_sorted_series_and_binary_search.md) | [A2](A2_memory_and_bits.md) |
| `06_config_publish_rollback` | [B4](B4_mutex.md) · [B6](B6_atomics_and_memory_order.md) · [B7](B7_lockfree_patterns.md) · [C1](C1_ring_buffer.md) | [A1](A1_c_refresher.md) (malloc/free 수명) |
| `07_badge_audit_dedupe` | [B1](B1_threads_and_scheduler.md) · [B5](B5_condvar_and_bounded_queue.md) · [B9](B9_lifecycle_and_shutdown.md) · [C6](C6_hashmap_open_addressing.md) | [C1](C1_ring_buffer.md) |
| `08_battery_energy_pipeline` | [B5](B5_condvar_and_bounded_queue.md) · [B8](B8_time_and_timeouts.md) · [C1](C1_ring_buffer.md) · [C3](C3_prefix_sum_and_range_query.md) | [C2](C2_sorted_series_and_binary_search.md) · [B9](B9_lifecycle_and_shutdown.md) |
| `09_wifi_scan_snapshot` | [B6](B6_atomics_and_memory_order.md) · [B7](B7_lockfree_patterns.md) · [C6](C6_hashmap_open_addressing.md) · [C7](C7_heap_and_topk.md) | [C1](C1_ring_buffer.md) |
| `10_heartbeat_watchdog` | [B1](B1_threads_and_scheduler.md) · [B6](B6_atomics_and_memory_order.md) · [B8](B8_time_and_timeouts.md) · [C7](C7_heap_and_topk.md) | [A2](A2_memory_and_bits.md) (캐시라인·false sharing) |

어느 문제든 공통으로 [A3](A3_build_and_tools.md)(컴파일·경고·TSan)과
[D1](D1_wrapper_recipe.md)(레시피)은 먼저 읽는다.

---

## 4. 코드로 보기 — 열 문제가 같은 다섯 함수다

어느 폴더를 열어도 헤더의 모양이 같다. 이름만 바뀐다.

```c
/* 01_gps_fix_cache/gps_cache.h */
int  gps_cache_init(void);
void gps_cache_deinit(void);
int  gps_get_last_fix(struct GpsFix *out);            /* Part 1: 최신값 */
int  gps_fix_at(uint64_t t, struct GpsFix *out);      /* Part 2: 시점 질의 */
double gps_distance_travelled(uint64_t t0, uint64_t t1);  /* Part 2: 범위 집계 */

/* 02_modem_rssi_window/rssi_window.h — 같은 다섯 자리 */
int  rssi_init(void);
void rssi_deinit(void);
int  rssi_get_latest(int *dbm, uint64_t *age_us);     /* Part 1 */
int  rssi_min_in_window(void);                        /* Part 2 */
double rssi_avg_in_window(void);                      /* Part 2 */
```

**패턴을 외운다**: `init` / `deinit` / 최신값 getter / 시점 질의 / 범위 집계.
`03`은 여기에 `try_pop` / `pop_timed`가 붙고, `05`와 `06`은 최신값 getter가
`acquire` / `release` 한 쌍이 되고, `10`은 최신값 getter 대신 `heartbeat`(쓰기)가 온다.
그래도 다섯 자리는 그대로다. 면접에서 API를 선언할 때 이 다섯 줄부터 쓰면 빈 종이를 마주하지 않는다.

---

## 5. 단계별로 만들어 보기 — 학습 계획 세 판

### v0 — 틀린 계획: `01`부터 순서대로 다 푼다

폴더 번호는 **난이도 순이 아니다.** `01`은 seqlock + 누적합으로 상급이고,
`08`은 최상급이다. 1번부터 시작하면 첫 문제에서 두 개의 새 개념(seqlock, prefix sum)을
동시에 만나고, 하루를 태우고 "역시 동시성은 어렵다"로 끝난다.

또 하나. 개념 노트를 **전부** 읽고 문제로 넘어가는 것도 v0다. B 트랙 9편과 C 트랙 7편을
연달아 읽으면 앞의 절반이 날아간다. 개념 하나 → 그걸 쓰는 문제 하나가 옳은 리듬이다.

### v1 — 고친 계획: 개념 한 편 → 그 개념을 쓰는 문제 한 개

§3.2의 열을 보고, ● 표시가 처음 등장하는 문제를 바로 푼다.
[B5](B5_condvar_and_bounded_queue.md)를 읽었으면 `03`을 푼다. `04`는 나중에 — 같은 B5지만
single flight라는 변형이 하나 더 얹혀 있다.

### v2 — 세 가지 경로

```
(a) 차근차근 (총 12~16시간)
    A1 A2 A3 A4 ─→ B1 B2 B3 B4 ─→ [03] ─→ B5 ─→ [04] ─→ B6 ─→ [02]
                              └─→ B7 ─→ [01] ─→ [05] ─→ [06]
    C1 C2 ─→ [05 재방문] ─→ C3 ─→ [08] ─→ C4 ─→ [02 재방문]
    C5 ─→ [03 재방문] ─→ C6 ─→ [07] ─→ C7 ─→ [09] ─→ [10]
    마지막에 D1 D2 D3 를 다시 읽고 원본 ALS 문제를 45분 타이머로 푼다

(b) 최소 코스 (총 4~5시간)   ← 면접이 이틀 뒤일 때
    D1 (레시피) ─→ B4 B5 B6 B9 C1 C2 만 읽는다
    [03] ─→ [01] ─→ [09]   세 문제
    D3 (말하는 법) ─→ 원본 ALS 문제를 45분 타이머로

(c) 약점별
    동시성이 약할 때:  B3 → B4 → [03] → B5 → [04] → B6 → [02] → B7 → [01] → [05] → [10]
    자료구조가 약할 때: C1 → [05] → C2 → [01] → C3 → [08] → C4 → [02]
                       → C5 → [03] → C6 → [07] → C7 → [09]
```

**(a) 차근차근** — 시간이 2주 있을 때. A 트랙은 하루, B 트랙과 짝지은 문제 네 개(03/04/02/01)로
사흘, lock-free 세 문제(01/05/06)로 이틀, C 트랙과 짝지은 나머지로 나흘. "재방문"은 같은 문제의
Part 2만 다시 푸는 것이다 — Part 1이 이미 손에 있으니 30분이면 된다.

**(b) 최소 코스** — 세 문제를 고른 이유가 각각 있다. `03`은 **가장 표준적인** 동시성 답안이라
아무 문제에서도 우선 이걸로 시작할 수 있다. `01`은 **원본 문제의 정확한 확장**이고 seqlock과
이진 탐색·누적합을 한 번에 준다. `09`는 **해시맵과 힙**이라 Part 2의 나머지 절반을 덮는다.
이 세 개로 §3.2와 §3.3의 ● 대부분이 한 번씩은 나온다.

**(c) 약점별** — 무엇이 약한지는 §7의 체크리스트로 판정한다. 둘 다 약하면 (b)를 먼저 하고
그 다음에 (c)의 약한 쪽으로 간다. 둘 다 약한 상태에서 (a)를 시작하면 끝까지 못 간다.

세 경로 모두 공통 규칙 셋. **`_solution.c`를 먼저 열지 않는다.** 30분 막히면 열되,
열고 나면 그날 안에 빈 파일에서 다시 짠다. **하네스를 고치지 않는다** — `main.c`와
`question_note`가 진실이다. **`./main.sh`가 전부 PASS 되기 전에 다음 문제로 넘어가지 않는다.**

---

## 6. 흔한 실수와 증상

계획과 학습 방법에서의 실수다. 코드 실수는 [D1 §6](D1_wrapper_recipe.md)에 있다.

| 실수 | 증상 | 왜 | 고치는 법 |
| --- | --- | --- | --- |
| `_solution.c`를 먼저 읽는다 | 읽을 때는 다 이해되는데 다음 날 빈 파일에서 한 줄도 못 쓴다 | 읽기는 인식(recognition), 푸는 것은 생성(generation). 다른 능력이다 | 30분 막힐 때까지 버틴다. 열었으면 같은 날 빈 파일에서 다시 짠다 |
| 폴더 번호 순서대로 푼다 | `01`에서 이틀 태우고 그만둔다 | 번호는 난이도 순이 아니다. `01`은 상, `08`은 최상 | §5의 (a) 또는 (b) 경로를 따른다. 첫 문제는 `03` |
| 개념 노트를 다 읽고 문제로 간다 | B 트랙 9편을 읽었는데 `03`을 열면 뭘 써야 할지 모른다 | 개념과 적용 사이 간격이 너무 길다 | 개념 1편 → 그 개념이 ●인 문제 1개. §3.2의 열로 짝을 찾는다 |
| Part 2만 골라 푼다 | Part 2는 되는데 45분 안에 둘 다는 못 끝낸다 | 실제 면접은 Part 1을 먼저 통과해야 Part 2가 나온다 | 언제나 `init`/`deinit`/getter부터. Part 1 20분 안에 컴파일 |
| `./main.sh`가 몇 개 FAIL인 채로 넘어간다 | 다음 문제에서 같은 버그를 또 낸다 | FAIL 하나가 개념 하나의 구멍이다 | 전부 PASS + `tsan`도 깨끗할 때까지. 남은 FAIL을 메모해 두고 그 개념 노트로 돌아간다 |
| 말하기 연습을 안 한다 | 코드는 되는데 스크리닝에서 또 막힌다 | 채점 기준 네 개 중 두 개(trade-off 정당화, 커뮤니케이션)는 코드가 아니다 | 문제를 풀 때마다 §3.4의 "Part 1 기법 / Part 2 자료구조"를 **소리 내어** 근거와 함께 말한다. [D3](D3_interview_talking.md) |
| `question_note` 하단 10문항을 건너뛴다 | 면접관 질문에 즉답이 안 나온다 | 그 10문항이 실제 면접에서 물어본 것의 재현이다 | 코드를 짜기 **전에** 종이에 답한다. 답이 안 나오는 항목이 다음에 읽을 노트다 |

---

## 7. 손으로 확인하기

### 7.1 실행

```sh
cd 03_event_tailer_shutdown
sed -n '/listening for/,$p' question_note   # 10문항 먼저
./main.sh                                   # 내 코드 (스텁은 FAIL이 뜬다)
./main.sh sol                               # 모범답안 (전부 PASS)
./main.sh fast                              # 버킷 eviction이 실제로 도는 축소 설정
```

변형 모드가 있는 문제는 그것도 돌린다. `01`은 `./main.sh window`(1.5초 윈도우, eviction 경계),
`04`/`08`/`09`/`06`은 `./main.sh tsan`, `06`은 `./main.sh asan`도 있다.

### 7.2 약점 판정

다음 다섯 문장에 5초 안에 답하지 못하면 **동시성이 약하다** → (c) 동시성 경로.

- mutex와 atomic 중 하나를 골라야 할 때 결정하는 질문 한 개는?
- condvar가 반드시 필요한 조건은?
- seqlock에서 독자가 재시도하는 조건은?
- release/acquire 짝이 만드는 happens-before 엣지는 어느 두 연산 사이인가?
- 타임아웃 없는 벤더 호출에 파킹된 스레드를 세우는 순서는?

다음 다섯 문장에 답하지 못하면 **자료구조가 약하다** → (c) 자료구조 경로.

- 링 버퍼에서 "ts ≤ t인 가장 최근 원소"를 찾는 이진 탐색의 인덱스 보정은?
- 누적합에서 tail을 evict할 때 무엇을 함께 고치나?
- 단조 덱이 값이 아니라 인덱스를 담는 이유는?
- 버킷을 쓰면 메모리가 이벤트 레이트와 무관해지는 이유는?
- 오픈 어드레싱에서 rehash를 못 할 때 "load factor 0.75"는 무슨 뜻인가?

### 7.3 진도 체크리스트

각 줄은 "`./main.sh`가 전부 PASS + 하단 10문항에 말로 답할 수 있다"를 뜻한다.

- [ ] `01_gps_fix_cache` — seqlock + 링·이진탐색·거리 누적합 (상 · 60분)
- [ ] `02_modem_rssi_window` — packed atomic + 단조 덱 2개 (중 · 50분)
- [ ] `03_event_tailer_shutdown` — 바운드 큐·condvar·종료 + 시간 버킷 (중 · 45분)
- [ ] `04_temp_single_flight` — single flight + 버킷 범위 질의 (중상 · 50분)
- [ ] `05_frame_latest_and_replay` — 트리플 버퍼·refcount + 예산 링 (상 · 60분)
- [ ] `06_config_publish_rollback` — 포인터 swap·refcount + 버전 링 (상 · 65분)
- [ ] `07_badge_audit_dedupe` — MPSC·순서 보존 + 오픈 어드레싱 해시 (상 · 70분)
- [ ] `08_battery_energy_pipeline` — 2단 파이프라인·토큰 버킷 + 사다리꼴 누적합 (최상 · 75분)
- [ ] `09_wifi_scan_snapshot` — 3슬롯 스냅샷 + 해시맵·min-heap (상 · 70분)
- [ ] `10_heartbeat_watchdog` — wait-free 저장 + lazy update min-heap (중상 · 55분)

---

## 8. 자가 점검

```check
Q: 개념 노트 24편 중 하나만 먼저 읽어야 한다면 어느 것이고, 왜인가?
A: C1(링 버퍼)이다. §3.3 표에서 C1은 열 문제 중 아홉 개에서 핵심(●)이다. 고정 메모리로 "최근
   윈도우"를 담는 기본 도구이고, 이진 탐색·누적합·단조 덱·버킷이 전부 그 위에 올라간다.
   동시성 쪽에서 하나만 고른다면 B3(race와 임계 구역) — 무엇이 문제인지 모르면 mutex도
   atomic도 고를 수 없다.

Q: 폴더 번호 순서대로 푸는 것이 왜 나쁜가? 첫 문제로 무엇을 추천하고 이유는?
A: 번호는 난이도 순이 아니다. 01은 seqlock + 누적합으로 상급이라 새 개념 두 개를 동시에 만난다.
   첫 문제는 03_event_tailer_shutdown이다. 이유는 셋. 바운드 큐 + condvar는 가장 표준적인 답안이라
   다른 문제에서도 우선 시도할 수 있고, 타임아웃 없는 호출의 종료 처리라는 이 유형의 핵심 함정을
   정면으로 다루고, Part 2의 시간 버킷이 링 버퍼의 가장 쉬운 응용이다.

Q: 2026-09-25 원본 ALS 문제는 이 열 문제 중 어느 것과 가장 가깝고, 무엇이 더해졌나?
A: 01_gps_fix_cache와 02_modem_rssi_window다. 원본은 Part 1이 최신 lux float 하나(4바이트)라
   mutex나 atomic이면 충분했고, Part 2가 "ts <= t인 가장 최근 값"이라 링 + 이진 탐색이었다.
   01은 payload를 40바이트 구조체로 키워 seqlock을 강제하고 Part 2에 거리 누적합을 얹었다.
   02는 staleness(값 + 나이를 한 덩어리로)와 슬라이딩 min/max를 얹었다.

Q: B7(lock-free 패턴)을 연습하려면 어느 문제를 골라야 하나? 표에서 어떻게 찾나?
A: §3.2에서 B7 열을 위에서 아래로 훑어 ●인 행을 고른다. 01, 05, 06, 09, 10 다섯 개다.
   난이도 순으로는 10(wait-free store가 가장 단순) → 01(seqlock) → 05(슬롯+refcount)
   → 09(가변 길이 + refcount) → 06(포인터 swap + 재활용 문제)로 올라가는 것이 낫다.

Q: 면접이 이틀 뒤다. 무엇을 하고 무엇을 버리나?
A: (b) 최소 코스다. D1(레시피)를 외우고, B4·B5·B6·B9·C1·C2만 읽고, 03 → 01 → 09 세 문제를 푼다.
   그리고 D3를 읽고 원본 ALS 문제를 45분 타이머로 소리 내어 풀어 본다.
   버리는 것: 06(포인터 swap 재활용), 08(파이프라인 + 토큰 버킷), 07(MPSC). 이 셋은 45분 문제로
   나올 가능성이 낮고 시간을 가장 많이 먹는다. 다만 "왜 그렇게 하는지"는 D1의 표로 말할 수 있게 둔다.

Q: `./main.sh`가 2개 FAIL인 상태로 다음 문제로 넘어가면 구체적으로 무엇을 잃나?
A: FAIL 하나는 보통 개념 하나의 구멍이다. 이 폴더의 하네스는 경계 조건과 종료 경로를 집중적으로
   검사하는데, 그 두 가지가 모든 문제에 반복해서 나온다. 그러니 지금 넘어가면 다음 문제에서
   같은 FAIL을 다시 본다. 남은 FAIL의 이름을 적고, §3.6 표에서 그 개념의 노트로 돌아가는 편이 빠르다.

Q: "동시성이 약하다"와 "자료구조가 약하다"를 어떻게 구분해서 판정하나?
A: §7.2의 두 묶음 다섯 문장에 각각 5초 안에 답해 본다. 동시성 쪽은 선택 기준·condvar 필요 조건·
   seqlock 재시도·release/acquire 엣지·종료 순서이고, 자료구조 쪽은 링 인덱스 보정·evict 시 부가
   구조·덱이 인덱스를 담는 이유·버킷의 메모리 독립성·rehash 없는 load factor다.
   둘 다 막히면 (b) 최소 코스를 먼저 하고, 그 다음 더 약한 쪽으로 (c)를 간다.
```

---

## 9. 요약 카드

- 열 문제는 **하나의 골격**이다: 벤더 → 전담 스레드 → 공유 상태 → 독자, 그리고 히스토리 질의.
- **번호는 난이도 순이 아니다.** 쉬운 순서는 `03` → `02` → `04` → `10` → `01` → `05` → `06` → `09` → `07` → `08`.
- **C1(링 버퍼)은 10문제 중 9문제에서 핵심**이다. 제일 먼저 손에 붙인다.
- **B7(lock-free)은 `01` `05` `06` `09` `10`** 다섯 문제에 몰려 있다. 그게 lock-free 코스다.
- 개념 노트 1편 → 그 개념이 ●인 문제 1개. 노트를 몰아 읽지 않는다.
- 경로 셋: **(a) 차근차근 12~16시간 / (b) 최소 4~5시간(`03`·`01`·`09`) / (c) 약점별**.
- **원본 ALS 문제는 이 지도의 가장 쉬운 구석**(4바이트 payload + 링·이진탐색)이다. `01`과 `02`가 그 확장.
- `_solution.c`를 먼저 열지 않는다. 열었으면 같은 날 빈 파일에서 다시 짠다.
- 문제를 열기 전에 `question_note` 하단 **10문항을 종이에 먼저** 답한다. 못 답한 항목이 다음에 읽을 노트다.
- 다음: [D3. 면접에서 말하는 법](D3_interview_talking.md) — 같은 코드로 점수를 더 받는 방법.
