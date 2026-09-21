# 🎞️ 실시간 버퍼링 (더블/트리플 버퍼) (Real-time Buffering (Double/Triple)) — Q31~38

> 리크루터 메일이 복습 주제로 **"real-time data handling patterns (such as double
> buffering)"** 을 대놓고 이름까지 적어줬다. 이 세트는 사실상 **출제 예고편**이다.
> 핵심 한 줄: **락으로 '데이터'를 보호하지 말고 '버퍼 소유권'을 보호한다.**
> 생산자(센서/DMA/카메라)는 절대 블로킹되지 않고, 소비자가 느리면 오래된 프레임을
> 버리되 **반드시 카운트한다**.

---

## 1. 핵심 아이디어

큐(FIFO)와 더블 버퍼는 목적이 다르다.

| | 큐 / 링버퍼 | 더블·트리플 버퍼 |
|---|---|---|
| 보존하고 싶은 것 | **모든 항목** (감사 로그, 명령) | **가장 최신 항목** (프레임, 스냅샷) |
| 가득 차면 | 블록하거나 oldest drop | 항상 최신으로 덮어씀 |
| 소비자가 느리면 | 지연이 누적된다 (latency 폭발) | 중간 프레임을 건너뛴다 (latency 일정) |
| 복사 | push/pop마다 데이터 복사 | **복사 0** — 포인터/인덱스만 교환 |

실시간 경로에서 latency 누적은 사실상 고장이다. 3초 전 프레임을 보여줄 바엔 버리는 게
낫다 — 그래서 "최신값 우선(latest-wins)" 버퍼링이다.

### 소유권 모델 — 버퍼는 언제나 셋 중 한 상태

```
                      mb_write_commit()            mb_read_acquire()
     ┌──────────┐    ───────────────▶  ┌───────┐  ───────────────▶  ┌──────────┐
     │  WRITER  │                      │ READY │                    │  READER  │
     │ 소유     │                      │(publish│                   │  소유    │
     │write_idx │                      │ 됨)   │                    │reader_idx│
     └──────────┘                      └───────┘                    └──────────┘
          ▲                                 │                             │
          │ mb_write_begin()                │ ◀── 새 프레임이 덮음        │ mb_read_release()
          │ (FREE를 집는다)                 │     (dropped++)             │
          │                                 ▼                             ▼
     ┌────┴──────────────────────────────────────────────────────────────────┐
     │                              FREE                                      │
     │        (어느 인덱스에도 잡혀 있지 않은 버퍼 = 아무나 써도 됨)          │
     └────────────────────────────────────────────────────────────────────────┘

  더블(nbuf=2): READER 1개 + READY 1개면 FREE가 없다
                → write_begin 이 READY를 희생(dropped++, starved++)
  트리플(nbuf=3): READER + READY + WRITER 가 동시에 성립 → writer 절대 안 굶음
```

핵심은 **인덱스 세 개(write_idx / ready_idx / reader_idx)가 곧 소유권 장부**라는 것.
mutex는 이 장부(int 3개)만 지킨다. 프레임 128B~6MB의 실제 바이트 write/read는
락 **밖**에서 일어난다 → 임계구역은 수 나노초, 우선순위 역전도 사실상 없다.

```c
Frame *f = mb_write_begin(&mb);   // 락: 인덱스 교환만 (ns)
frame_fill(f, seq);               // 락 밖: 진짜 작업 (us~ms)
mb_write_commit(&mb);             // 락: 인덱스 교환만 (ns)
```

---

## 2. 더블 vs 트리플 — 무엇을 사느냐

| | 더블 버퍼 (N=2) | 트리플 버퍼 (N=3) |
|---|---|---|
| 메모리 | 2× 프레임 | 3× 프레임 (**+50%**) |
| writer가 막히거나 굶는가 | 굶는다(FREE 없음) → READY를 뺏음 | **절대 안 굶는다** |
| reader가 든 프레임 | 안전(절대 안 뺏음) | 안전 |
| 소비자 처리시간 허용치 | **전송 1주기 이내** | 1주기를 넘어도 됨 |
| 남는 drop 종류 | starve-drop + 덮어쓰기 drop | 덮어쓰기 drop만 |
| 지연(latency) | 최소 | +0~1 프레임 |
| 전형적 용도 | DMA ping-pong, MCU (RAM 부족) | GPU/디스플레이, Linux 영상 파이프라인 |

**면접 포인트**: 트리플 버퍼도 drop이 0이 되진 않는다. 없어지는 건 "writer가 쓸 곳이
없어 굶는 drop(starved)"뿐이고, "소비자가 느려 최신 프레임이 이전 READY를 덮는 drop"은
그대로 남는다. **둘을 카운터로 분리하면** 현장에서 "생산이 빠른가, 소비가 느린가"를
텔레메트리만 보고 안다. 그리고 1080p RGB면 N=2는 ~6MB, N=3은 ~9MB — RAM 예산에서
3MB는 공짜가 아니므로 "트리플이 항상 정답"이 아니라 **trade-off를 말해야** 한다.

---

## 3. latest-value 슬롯 — mutex vs seqlock

작은 POD 구조체(링크 통계, 배터리 SoC, GPS fix) 하나만 최신으로 공유할 때는
버퍼 풀까지 갈 필요가 없다.

| | mutex | seqlock |
|---|---|---|
| writer | reader가 잡고 있으면 **막힌다** | **절대 안 막힌다** |
| reader | 막힐 수 있음 | 막히지 않고 **재시도**한다 |
| 적합 | 쓰기 잦음 · 페이로드 큼 · 공정성 필요 | **읽기 多 · 쓰기 少 · 페이로드 小** |
| 제약 | 없음 | writer 1명, 포인터 없는 POD만 |
| 우선순위 역전 | 가능 (PI mutex 필요) | 구조적으로 없음 |
| 굶주림 | 없음 | writer가 폭주하면 **reader가 못 읽음** |

```c
// writer: seq 홀수(진입) → 데이터 → seq 짝수(이탈)
seq++;  release_fence;  payload = v;  release_fence;  seq++;

// reader: 앞뒤 seq가 같고 짝수일 때만 채택, 아니면 재시도(상한 필수)
s1 = seq;  if (s1 & 1) retry;
tmp = payload;  acquire_fence;
if (seq != s1) retry;   // 읽는 동안 writer가 지나감
```

> ⚠️ seqlock 페이로드는 반드시 `_Atomic` + `memory_order_relaxed` 로 읽고 쓸 것.
> 일반 변수로 두면 "읽고 버리는" 경합이 알고리즘상 정상이어도 **C 표준상 data
> race = UB** 이고, ThreadSanitizer도 경고한다. relaxed atomic + fence면 생성
> 코드는 사실상 같고 UB가 아니다. 이 문장 하나로 memory model 이해도를 보여줄 수 있다.
>
> ⚠️ 페이로드에 **포인터가 있으면 seqlock 금지**. 찢어진 포인터를 역참조하면
> 재시도할 기회조차 없이 죽는다. 그래서 커널 seqlock도 값 스냅샷(`jiffies`,
> `timekeeper`)에만 쓴다.

---

## 4. 레이트 디커플링 — 400Hz → 100Hz

생산 레이트와 소비 레이트가 다를 때 4:1로 줄이는 두 정책:

| 정책 | 무엇을 하나 | 어울리는 데이터 | 부작용 |
|---|---|---|---|
| **latest (take-newest)** | 블록의 마지막 샘플 | 계단·이벤트성: 링크 up/down, 도어 상태, 최신 GPS fix, 재부팅 플래그 | 노이즈·에일리어싱 그대로 통과 |
| **mean (block average)** | 블록 평균 | 연속 아날로그: RSSI, 온도, 전류, PM2.5 | 엣지가 뭉개짐, 지연 +½블록 |

판단 기준 한 문장: **"이 신호는 순간값이 의미인가, 추세가 의미인가?"**
상태 전이를 놓치면 안 되는 신호에 평균을 쓰면 글리치가 사라져 버린다.
반대로 노이즈 심한 아날로그에 latest를 쓰면 무작위 한 점이 제어에 들어간다.

구현 디테일(감점 포인트): 합계는 `int64_t` 누적(int32면 오버플로), 반올림은 **0에서 먼
쪽**으로 통일(C의 `/`는 0쪽 절단이라 음수 편향), 꽉 찬 블록만 처리하고 꼬리는 다음
호출로(스트리밍 친화). 그리고 이 계산을 **순수 함수**로 뽑아 하드웨어 없이 테스트하는
것 자체가 메일의 "modular, testable embedded software" 항목에 대한 답이다.

---

## 5. DMA ping-pong — ISR에서 하면 안 되는 것

```
   시각 t0        t1        t2        t3
   DMA  →buf0     →buf1     →buf0     →buf1
   CPU            proc buf0 proc buf1 proc buf0
        ↑ISR      ↑ISR      ↑ISR      ↑ISR   ← 각 ISR이 하는 일: 인덱스 토글뿐
```

완료 인터럽트에서 하는 일은 **인덱스 교환 한 줄**이다.

- ❌ 데이터 복사 / 파싱 / 필터링 — ISR은 수 µs 안에 끝나야 한다
- ❌ mutex, malloc, printf 등 블로킹 API — 애초에 호출 불가
- ✅ `active ^= 1`, `full = done`, 카운터 증가, 태스크 깨우기 시그널만

### ⚠️ 캐시 일관성 (실제 하드웨어에서 "가끔 깨지는" 버그의 원흉)

DMA는 CPU 캐시를 우회해 DRAM에 직접 읽고 쓴다. 그래서:

| 방향 | 필요한 것 | 안 하면 |
|---|---|---|
| **DMA → CPU** (수신 완료 후 읽기 전) | `invalidate` (오래된 캐시 라인 폐기) | CPU가 **옛날 데이터**를 캐시에서 읽는다 |
| **CPU → DMA** (송신 버퍼 채운 뒤) | `clean` / `flush` (쓰기 버퍼를 DRAM에 반영) | DMA가 **덜 쓰인 버퍼**를 전송한다 |

버퍼는 **캐시 라인 정렬 + 라인 크기의 배수**여야 한다. 아니면 같은 라인에 얹힌
인접 변수까지 함께 무효화/플러시되어 "재현 안 되는" 데이터 손상이 된다.

Linux에서는 보통 직접 안 한다 — **스트리밍 매핑**(`dma_map_single()` /
`dma_unmap_single()` / `dma_sync_single_for_cpu|device`)이 방향에 맞춰 대신 해주고,
**일관 매핑** `dma_alloc_coherent()` 는 아예 논캐시 메모리를 준다. 작고 자주 접근하면
coherent, 크고 한 번에 처리하면 streaming. (SSD 펌웨어의 DMA 완료 인터럽트 경험을
이 어휘로 번역해 말하면 "임베디드 Linux 경험이 얕다"는 갭을 그대로 덮을 수 있다.)

---

## 6. 프레임 지터/누락 측정

타임스탬프 배열만 받는 **순수 함수**로 만든다(카메라도 커널도 없이 단위 테스트 가능).

```c
k = (gap + nominal/2) / nominal;   // 반올림!
if (k > 1) missed += k - 1;        // k개 주기 = k-1개 누락
```

**반올림이 핵심**이다. 절단(`gap / nominal`)을 쓰면 14ms 간격(정상 지터 범위)이
누락 0으로, 19.9ms가 누락 0으로 잡혀 통계가 망가진다. 반올림하면 "가장 가까운
주기 배수"로 해석되어 ±40% 지터에도 오판하지 않는다.

같이 뽑을 값: `max_gap`(최악 지연), `min_gap`, `max_jitter = max|gap−nominal|`, 그리고
**`nonmonotonic`(시계 역행 횟수)**. 마지막 게 중요하다 — 필드에서는 NTP 점프나
suspend/resume으로 타임스탬프가 거꾸로 가고, 이걸 누락과 뒤섞으면 "밤마다 프레임
5000개 누락" 같은 유령 버그 리포트가 나온다. 그래서 실시간 측정은 항상
`CLOCK_MONOTONIC`이고 절대 `CLOCK_REALTIME`이 아니다.

---

## 7. 흔한 함정 (인터뷰 감점 포인트)

- **락으로 프레임 전체를 감싸기** — 락 안에서 memcpy 하는 순간 실시간성이 죽는다. 락은 인덱스만.
- **reader가 든 버퍼를 writer가 뺏기** — 그게 정확히 tearing이다. 장부에 reader_idx가 있는 이유.
- **drop을 조용히 하기** — 카운터 없이 버리면 필드에서 절대 못 잡는다. 반드시 텔레메트리로.
- **starve-drop과 덮어쓰기 drop을 한 카운터에 합치기** — 원인 진단 불가.
- **`volatile`로 해결하려 하기** — 최적화 억제일 뿐 원자성도 순서도 없다. 스레드 간에는 `_Atomic`.
- **seqlock**: reader 재시도 상한 없음(무한 루프) / 페이로드가 일반 변수(UB+TSan) / 페이로드에 포인터(즉사).
- **평균 누적을 int32로** — 오버플로. 반올림을 0쪽 절단으로 — 음수 편향.
- **DMA 버퍼 캐시 유지 누락 / 캐시 라인 정렬 안 맞음** — 재현 안 되는 손상.
- **테스트를 "읽힌 프레임 수 == N"으로 단정** — 플래키하다. 대신 불변식 `read + dropped + leftover + inflight == produced`.

---

## 8. 면접에서 말할 것 (한국어 + 영어)

### 설계 선언 (코딩 시작 전 30초)
- KO: "락으로 데이터를 보호하지 않고 **버퍼 소유권**을 보호하겠습니다. 임계구역은
  인덱스 세 개를 바꾸는 것뿐이고, 프레임 복사는 락 밖에서 합니다."
- EN: **"I protect ownership, not the data. The critical section only swaps three
  indices — the frame bytes are written and read outside the lock."**

### 왜 블로킹하지 않는가
- KO: "생산자는 센서 타이밍에 묶여 있어서 소비자를 기다릴 수 없습니다. 그래서
  `write_begin`은 항상 성공하고, 대신 가장 오래된 프레임을 버리고 카운트합니다."
- EN: "The producer is bound to sensor timing, so it can never wait on the consumer.
  `write_begin` always succeeds; if nothing is free I sacrifice the oldest frame
  and increment a drop counter."

### drop은 반드시 관측 가능하게
- EN: **"A dropped frame is fine. A silently dropped frame is a bug — I always
  export it as a counter so the fleet telemetry shows it."**

### 더블 vs 트리플 trade-off
- KO: "버퍼를 하나 더 두면 writer가 굶는 경우가 사라집니다. 대가는 메모리 50%
  증가고, 지연은 최대 한 프레임 늘어납니다. RAM 예산에 따라 고르겠습니다."
- EN: "A third buffer removes the case where the writer has nowhere to write. The
  price is 50% more memory and up to one extra frame of latency — with 1080p RGB
  that's 6MB versus 9MB, so on a constrained gateway it's a real decision."

### tearing을 어떻게 테스트하나
- EN: **"I make the payload self-describing — every byte is derived from the
  sequence number — so a torn frame is detectable by inspection. Then I assert an
  invariant instead of a count: frames read plus frames dropped plus what's still
  in flight must equal frames produced. That test can't be flaky."**

### seqlock을 언제 쓰나
- EN: "For a small POD snapshot that's read far more often than it's written, I'd
  use a seqlock so the writer never blocks. It needs a single writer and no
  pointers in the payload, and the reader must have a retry bound. If either
  condition fails, I use a mutex — it's simpler and always correct."

### ISR / DMA
- EN: **"In the completion ISR I only swap indices — no copying, no parsing, no
  locks. And on real hardware I'd invalidate the cache before the CPU reads a
  DMA-filled buffer and clean it before the DMA reads a CPU-filled one, with the
  buffers aligned to a cache line."**

### 모를 때
- EN: "I haven't wired that specific Linux DMA API in production. The equivalent
  problem in my SSD firmware work was the NAND DMA completion path — here's how
  I'd reason about it, and I'd verify the exact `dma_map_single` semantics against
  the kernel docs before committing."

---

## 9. 체크리스트

- [ ] 소유권 4상태(WRITER/READY/READER/FREE) 전이를 손으로 그려 설명할 수 있다
- [ ] `write_begin`이 블로킹하지 않는 이유 + reader 버퍼를 안 뺏는 이유(tearing)를 말한다
- [ ] `nbuf`만 3으로 바꿔 트리플이 되게 짜고, 메모리 +50% / 지연 +1프레임을 정당화한다
- [ ] starve-drop과 덮어쓰기 drop을 분리해 세고, 트리플이 없애는 건 전자뿐임을 안다
- [ ] 회계 불변식 `read + dropped + leftover + inflight == produced` 를 즉시 쓴다
- [ ] tearing 검출용 self-describing 페이로드를 설계하고, 카운트가 아닌 불변식으로 단정한다
- [ ] mutex 슬롯과 seqlock을 둘 다 5분 안에 짜고, 3대 제약(단일 writer/POD/재시도 상한)을 말한다
- [ ] seqlock 페이로드에 relaxed atomic을 쓰는 이유(UB + TSan)를 설명할 수 있다
- [ ] latest vs mean 데시메이션을 신호 종류로 나눠 정당화한다 (int64 누적 + 반올림 방향)
- [ ] ISR 금지 3가지 + DMA 캐시 invalidate/clean 방향 + `dma_alloc_coherent` vs `dma_map_single`
- [ ] 지터/누락에 반올림을 쓰는 이유와 `CLOCK_MONOTONIC`을 쓰는 이유를 말한다
- [ ] TSan을 CI에 넣겠다고 먼저 말한다
