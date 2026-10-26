# V12 · Multi-Camera Frame Timestamp & Sync Checker

> **ID**: V12 · **Priority**: P1 · **Pattern**: wrap-safe counter delta + two-pointer nearest match · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh V12` / `./run.sh V12 sol`

## 1. Problem

An autonomy SoC receives frames from several cameras over MIPI CSI-2. For every frame the capture path logs a record: the camera's **16-bit hardware frame counter** (wraps 65535 → 0) and a **64-bit timestamp in ns** from a PTP-synced clock, taken at start-of-frame. Nominal rate is 30 fps (period `P = 33,333,333 ns`). Write the checker a validation run uses to decide whether the capture path and the camera sync are healthy.

```c
typedef struct { uint16_t frame_counter; uint64_t ts_ns; } FrameRec;

typedef struct {
    uint32_t frames, dropped, duplicates, out_of_order;
    uint32_t pairs;                          /* consecutive (+1) pairs used for timing */
    uint64_t dt_min_ns, dt_max_ns, dt_mean_ns, max_jitter_ns;   /* jitter = |dt - P| */
} StreamStats;

typedef struct {
    uint32_t ref_frames, complete, incomplete;
    uint64_t max_skew_ns;
    uint32_t max_skew_idx;
} SyncStats;

int stream_stats(const FrameRec *f, size_t n, uint64_t period_ns, StreamStats *st);
int sync_check(const FrameRec *const cam[], const size_t n[], size_t ncam,
               uint64_t period_ns, uint64_t limit_ns,
               SyncStats *ss, uint32_t *flagged, size_t flagged_cap);   /* # flagged or -EINVAL */
```

**`stream_stats`** (one pass, integer math). With `d = (uint16_t)(cur.counter − prev.counter)`:

- `d == 0` → duplicate (baseline unchanged)
- `d >= 0x8000` → counter went backwards → out-of-order (baseline unchanged)
- otherwise → `dropped += d − 1`; if `cur.ts <= prev.ts` it is out-of-order; else if `d == 1` it is a **timing pair** with `dt = cur.ts − prev.ts`. Baseline becomes `cur`.
- Over timing pairs: `dt_min`, `dt_max`, `dt_mean` (floor), `max_jitter = max |dt − P|`. All 0 when there are no pairs.

**`sync_check`**. Camera 0 is the reference. For each reference frame `i`, find in every other camera the frame with the **nearest timestamp**; it matches only if `|diff| <= P / 2`. `skew(i) = max − min` timestamp over the reference frame and its matches. Count `complete` (every camera matched) and `incomplete`. Track the max skew (first index wins on ties). Flag `i` when `skew(i) > limit_ns`: write up to `flagged_cap` indices and **return the total**. Bad arguments → `-EINVAL`.

### Example

```
P = 33,333,333 ns, limit = 1,000,000 ns, 6 frames, timestamps relative to 5 s
cam0 (ref): 0, 33,333,333, 66,666,666, 100,000,000, 133,333,333, 166,666,666
cam1      : same + 50,000 ns, frame 4 dropped (counters 7 8 9 10 _ 12)
cam2      : same + 400,000 ns x frame index (free-running, not PTP-locked)

sync_check -> 3, flagged = {3, 4, 5}
  skew per frame = 50,000 / 400,000 / 800,000 / 1,200,000 / 1,600,000 / 2,000,000
  ref_frames 6, complete 5, incomplete 1 (frame 4), max_skew 2,000,000 at frame 5
stream_stats(cam1) -> frames 5, dropped 1, pairs 3
stream_stats(cam2) -> dt_min 33,733,333, dt_max 33,733,334, max_jitter 400,001
```

### Constraints

- Each stream is sorted by arrival; for `sync_check` its timestamps are non-decreasing. Up to `MAX_CAM = 8` cameras.
- No malloc, no floating point. O(total frames) for `sync_check`.
- Streams may be empty (`n == 0`, pointer may be `NULL`).

### Clarifying questions to ask (first 3 minutes)

1. Where is the timestamp taken — CSI-2 frame-start packet at the receiver, end-of-frame, or in the driver?
2. Are the cameras hardware-triggered from one source, or free-running and only timestamped by PTP?
3. How wide is the HW frame counter, and can it reset (camera re-init) as opposed to wrap?
4. What is the skew limit, and is it measured against a reference camera or as max − min across all?
5. A frame missing in one camera: skip it, or report the frame as incomplete?
6. Is jitter judged against the nominal period, or against the stream's own mean period (to separate jitter from clock drift)?

## 2. Your workspace

`./run.sh V12`로 바로 테스트하세요. 테스트 8묶음이 있고, `make_cam()`이 30 fps 타임스탬프(`1e9 × k / 30`, 정수)를 만들어 줍니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 16비트 랩은 뺄셈 한 번</summary>

`(uint16_t)(cur - prev)`는 65535 → 0에서 1이 나옵니다. 결과가 `0x8000` 이상이면 "뒤로 간 것"으로 봅니다(TCP sequence 비교와 같은 규칙). 비교 연산자(`cur > prev`)로는 랩을 못 다룹니다.

</details>

<details>
<summary>Hint 2 — 지터는 연속(+1) 쌍에서만</summary>

프레임이 빠지면 간격이 2P가 됩니다. 이걸 지터에 넣으면 min/max가 망가집니다. drop은 drop대로 세고, 타이밍 통계는 `d == 1`인 쌍만 씁니다.

</details>

<details>
<summary>Hint 3 — 가장 가까운 프레임은 two-pointer로</summary>

카메라마다 커서 `pos[c]`를 둡니다. 기준 시각 `t` 이하인 마지막 프레임까지 커서를 밀고, 후보는 `k`와 `k+1` 두 개뿐입니다. 기준 프레임이 시간순이라 커서는 뒤로 가지 않으므로 전체 O(Σn)입니다. 차이가 `P/2`보다 크면 "그 카메라는 이 프레임을 놓쳤다".

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 프레임 정렬과 skew

<div class="chart" data-src="chart1.svg" data-caption="예제: cam2는 PTP에 안 묶여 프레임마다 0.4 ms씩 밀림(그림은 ×8 과장). frame 3부터 1 ms 초과 → 3개 flag. frame 4는 cam1이 빠져서 ±P/2 창 안에 후보가 없음 → incomplete"></div>

두 함수는 서로 다른 질문에 답합니다.

- `stream_stats`: **한 카메라 안**에서 캡처 경로가 프레임을 잃거나, 두 번 내거나, 순서를 바꾸는가? 간격은 안정적인가?
- `sync_check`: **카메라 사이**에서 같은 순간을 찍었는가? 프레임 인덱스로 맞추면 안 됩니다. 카메라마다 drop이 다르면 인덱스가 어긋나기 때문에 **타임스탬프로 정렬**합니다.

cam2처럼 offset이 계속 커지는 건 "고정 offset"이 아니라 **클럭 드리프트**입니다. 0.4 ms / 33.3 ms = 12,000 ppm라 과장된 값이지만, 실제로 PTP lock이 풀린 카메라는 수십 ppm로 천천히 벌어집니다. 장시간 테스트에서 skew가 시간에 비례해 커지면 드리프트, 한 번 튀면 지터나 트리거 문제입니다.

### 4.2 카운터 랩과 gap

<div class="chart" data-src="chart2.svg" data-caption="위: 65535 → 0은 d = 1이라 정상, 0 → 2는 d = 2라 drop 1. 아래: 늦게 온 프레임 2는 d = 0xFFFF로 OOO 처리하고 기준(3)은 그대로 둬야 다음 4가 d = 1이 됨"></div>

늦게 온 프레임에 기준을 옮기면(baseline = 2) 다음 프레임 4에서 d = 2가 나와 **없는 drop을 하나 더** 셉니다. 그래서 OOO일 때는 baseline을 안 바꿉니다.

### 4.3 손으로 따라가기 (trace)

`stream_stats` — 테스트 4번 `{0,1,1,3,2,4}`:

| i | counter | ts (rel) | d | 판정 | dropped | dup | OOO | pairs | baseline |
|---|---|---|---|---|---|---|---|---|---|
| 0 | 0 | 0 | – | 첫 프레임 | 0 | 0 | 0 | 0 | 0 |
| 1 | 1 | 33,333,333 | 1 | pair, dt = 33,333,333 | 0 | 0 | 0 | 1 | 1 |
| 2 | 1 | 33,334,333 | 0 | duplicate | 0 | 1 | 0 | 1 | 1 |
| 3 | 3 | 100,000,000 | 2 | drop 1 (pair 아님) | 1 | 1 | 0 | 1 | 3 |
| 4 | 2 | 66,666,666 | 0xFFFF | OOO, baseline 유지 | 1 | 1 | 1 | 1 | 3 |
| 5 | 4 | 133,333,333 | 1 | pair, dt = 33,333,333 | 1 | 1 | 1 | 2 | 4 |

결과: `dt_min = dt_max = dt_mean = 33,333,333`, `max_jitter = 0`.

`sync_check` — 예제 (ts는 5 s 기준 상대값, P/2 = 16,666,666):

| i | cam0 (ref) | cam1 match | cam2 match | skew | 판정 |
|---|---|---|---|---|---|
| 0 | 0 | 50,000 | 0 | 50,000 | ok |
| 1 | 33,333,333 | 33,383,333 | 33,733,333 | 400,000 | ok |
| 2 | 66,666,666 | 66,716,666 | 67,466,666 | 800,000 | ok |
| 3 | 100,000,000 | 100,050,000 | 101,200,000 | 1,200,000 | **flag** |
| 4 | 133,333,333 | 없음 (최근접 100,050,000, 차이 33,283,333 > P/2) | 134,933,333 | 1,600,000 | **flag**, incomplete |
| 5 | 166,666,666 | 166,716,666 | 168,666,666 | 2,000,000 | **flag**, max |

다른 테스트에서 확인하는 숫자:

| 테스트 | 결과 |
|---|---|
| 1. 깨끗한 3대 (offset 0 / +50 µs / −120 µs), 10프레임 | pairs 9, dt_min 33,333,333, dt_max 33,333,334, mean 33,333,333, jitter 1 · skew 170,000, flag 0 |
| 2. counter 104 빠짐 | frames 9, dropped 1, pairs 7 |
| 3. 65533…2 / 65534, 65535, 1 / 65533…3 (1 빠짐) | dropped 0 / 1 / 1, pairs 5 / 1 / 4 |
| 5. frame 5가 2 ms 늦음 | dt_min 31,333,334, dt_max 35,333,333, mean 33,333,333, jitter 2,000,000 |
| 6. cam2 +2 ms 고정 | 10개 전부 flag (cap 4라 4개만 기록), max skew 2,000,000 at 0 · +1 ms 정확히 → flag 0 (`>` 이므로) |
| 8. cam1 비어 있음 / ref 비어 있음 | incomplete 6, flag 3 / ref_frames 0, return 0 |

dt가 33,333,333과 33,333,334로 번갈아 나오는 이유: 1/30 s는 정수 ns로 안 떨어집니다. `1e9 × k / 30`으로 만든 타임스탬프는 3프레임마다 1 ns를 보정합니다. 그래서 "jitter 1 ns"는 정상이고, limit은 이보다 훨씬 크게 잡습니다.

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `absdiff()`: `uint64_t` 두 개를 빼서 절댓값을 구할 때 `a - b`를 먼저 하면 음수가 큰 양수로 바뀝니다. 크기 비교 후 큰 쪽에서 뺍니다.
- `dt_min`은 0으로 초기화되어 있으니 `pairs == 0`일 때 첫 값으로 설정합니다(`UINT64_MAX`로 초기화하면 pair가 없을 때 결과에 남음).
- 합계 `sum`은 `uint64_t`라 30 fps로 수천 년치까지 안 넘칩니다. `dt_mean`은 floor 나눗셈입니다.
- `sync_check`의 `while (k + 1 < n[c] && s[k+1].ts <= t)`가 커서를 앞으로만 밀기 때문에 기준 프레임 전체에 대해 각 카메라 배열을 한 번만 훑습니다.
- `flagged`는 `cap`까지만 쓰고 개수는 끝까지 셉니다(`snprintf` 규칙).

### 4.5 복잡도

- `stream_stats`: 시간 O(n), 메모리 O(1).
- `sync_check`: 시간 O(Σ n[c]) (two-pointer), 메모리 O(MAX_CAM) 커서. 이진 탐색으로 하면 O(n₀ · ncam · log n)이라 더 느립니다.

---

## 5. 임베디드 관점으로 말할 것

- **타임스탬프를 어디서 찍나가 정확도를 정합니다**: CSI-2 수신기가 Frame Start short packet을 받는 순간 HW로 PTP 시간을 래치하는 게 가장 정확합니다(수 µs 이하). 드라이버의 IRQ 핸들러에서 찍으면 인터럽트 지연(수십 µs~ms)이 지터로 섞입니다. 같은 checker로 두 방식을 비교하면 지터 차이가 바로 보입니다.
- **카메라 노출 시점 ≠ SOF 시점**: rolling shutter는 줄마다 노출 시각이 다르고, SOF와 노출 중심 사이에 고정 지연이 있습니다. 모델이 다른 카메라를 섞으면 이 고정 지연이 skew offset으로 보이므로 보정 테이블이 필요합니다.
- **HW trigger vs free-running**: 모든 카메라를 같은 트리거 신호(FSIN/XVS)로 찍으면 skew는 트리거 배선 + 센서 내부 지연 수준입니다. free-running이면 PTP만으로는 "시계는 같아도 찍는 순간이 다름" → 위 예제 cam2처럼 드리프트가 생깁니다.
- **결과는 숫자로 남긴다**: drop/dup/OOO/jitter/skew를 카메라별 카운터로 남기면 장시간 soak, 온도 corner, DDR 대역폭 스트레스([V3](../V3_dma_desc_ring/index.html) DMA 동시 실행) 아래에서 회귀를 비교할 수 있습니다. CSI-2 패킷 레벨 검사는 [V9](../V9_csi2_packet_parse/index.html).

## 5.1 면접관(HW 출신 Sr Manager)이 파고들 포인트

- **"How do you validate sync on real silicon, not just trust the timestamps?"** → 타임스탬프 자체가 틀릴 수 있으니 **외부 기준**이 필요합니다: 모든 카메라 시야에 LED를 두고 알려진 시각(PTP PPS에 묶인 GPIO)에 깜빡여 각 프레임에서 LED가 켜진 줄/프레임을 찾습니다. 트리거 라인은 스코프로 직접 잽니다. *"I close the loop with an external reference: a PPS-locked LED strobe in every camera's view, and a scope on the trigger lines."*
- **"How do you know your checker is correct?"** → 합성 스트림에 drop, wrap, dup, late frame, 고정 offset, 드리프트를 **정확한 개수만큼** 넣고 결과 숫자가 맞는지 봅니다(이 테스트들이 그것). 경계값(정확히 1 ms → flag 안 됨)도 고정합니다. *"Every fault class has a synthetic stream with a known answer, including the exact-threshold boundary."*
- **"64-bit timestamp from a 32-bit bus — what can go wrong?"** → 하위 32비트를 읽은 뒤 상위를 읽는 사이에 carry가 넘어가면 4.29 s 튀는 값이 나옵니다(torn read). HW가 LO 읽기 때 HI를 래치하게 하거나, `hi1 → lo → hi2` 읽고 `hi1 != hi2`면 다시 읽습니다. 이 checker에서는 **dt가 ±2^32 ns 근처로 튀는 프레임**으로 나타납니다. *"A torn hi/lo read shows up as a ±4.29 s jump in dt; the fix is a HW latch or the hi-lo-hi retry loop."*
- **"What about clock domain crossing in the timestamp unit?"** → 카메라 픽셀 클럭 도메인의 SOF 이벤트를 PTP 시계 도메인으로 넘길 때 동기화기 2~3 사이클 지연과 그 ±1 사이클 불확실성이 생깁니다. 고정 지연은 보정, ±1 사이클은 지터 바닥으로 문서화합니다. *"The CDC adds a fixed latency we calibrate out plus one-cycle uncertainty, which sets the jitter floor."*
- **"How long do you run it?"** → drop률 1e-6을 말하려면 백만 프레임 이상(30 fps로 약 9시간)이 필요합니다. 그래서 짧은 SLT 버전(수천 프레임, 큰 오류만)과 긴 soak/PVT 버전을 나눕니다. *"Short run for gross failures in SLT; hours-long soak across temperature for rate claims."*

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Explain PTP / gPTP (802.1AS) briefly. | grandmaster 시계를 기준으로 Sync/Follow_Up(시각), Pdelay_Req/Resp(링크 지연) 메시지를 HW 타임스탬프로 주고받아 각 노드가 offset과 rate를 맞춥니다. 802.1AS(gPTP)는 차량/AVB용 프로파일로 모든 노드가 time-aware(경로 지연을 홉마다 측정)라 sub-µs 정확도가 나옵니다. |
| Hardware trigger vs free-running cameras? | trigger: 한 신호로 노출 시작 → skew는 µs 수준, 대신 배선과 센서 trigger 모드 지원 필요. free-running: 배선 없음, 카메라 내부 클럭이 각자라 드리프트 → PTP로 타임스탬프는 맞아도 **촬영 시각은 다름**. 센서 퓨전에는 trigger가 유리합니다. |
| Where should the timestamp be taken? | CSI-2 수신기에서 Frame Start packet 도착 시 HW 래치가 최선. 드라이버/ISR은 지연과 지터가 큼. 노출 시각을 원하면 SOF 기준에 센서별 고정 지연(노출 시간/2 등)을 보정합니다. |
| How would you read a 64-bit timer safely on a 32-bit bus? | HW 래치(LO 읽을 때 HI 스냅샷)가 있으면 LO→HI 순서를 지킵니다. 없으면 `do { h1 = HI; l = LO; h2 = HI; } while (h1 != h2);`. 64-bit 단일 접근(AXI 64-bit)이 되면 그게 제일 간단합니다. |
| The skew limit is violated only sometimes. How do you debug? | 시간축으로 skew를 그립니다. 선형 증가 → 드리프트(PTP lock 상태 확인). 주기적 튐 → 다른 트래픽과 간섭(인터럽트/DDR). 랜덤 단발 → 타임스탬프 경로(torn read, CDC). 같은 순간 다른 카메라도 튀는지로 공통 원인 여부를 나눕니다. |
| A late frame is counted both as a gap and as out-of-order. Fix it? | 작은 재정렬 창(예: 최근 8개 counter 비트맵)을 두고, 창 안에서 늦게 온 counter는 drop에서 빼고 `reordered`로 셉니다. 네트워크 패킷 재정렬 검출과 같은 방식입니다. |
| Separate jitter from drift? | dt의 평균이 P와 다르면 rate 오차(드리프트), 평균 주변의 흩어짐이 지터입니다. `max |dt − mean|`을 따로 계산하거나, 타임스탬프에 직선 회귀를 해서 기울기(ppm)와 잔차(지터)로 나눕니다. |

## 7. 자주 하는 실수

- `cur > prev`로 counter 비교 → 65535 → 0에서 가짜 OOO + 가짜 drop (테스트 3번)
- drop이 있는 간격(2P)을 지터 통계에 넣음 → dt_max가 66 ms (테스트 2번 `dt_max == 33,333,334`)
- 늦게 온 프레임에서 baseline을 옮김 → 다음 프레임에서 drop을 하나 더 셈 (테스트 4번)
- 프레임 **인덱스**로 카메라를 맞춤 → 한 카메라가 drop하면 그 뒤가 전부 33 ms 어긋나 flag 폭주 (테스트 7번)
- `uint64_t` 차이를 `a - b`로 바로 계산 → 음수가 거대한 값이 되어 nearest 판정이 틀림 (offset −120 µs 카메라, 테스트 1번)
- skew를 `>=`로 비교 → 정확히 1 ms에서 flag (테스트 6번 경계)
- `flagged_cap`에서 개수 세기를 멈춤 → 반환값이 10이 아니라 4 (테스트 6번)
