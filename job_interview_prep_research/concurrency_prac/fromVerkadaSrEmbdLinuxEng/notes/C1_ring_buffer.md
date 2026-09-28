# C1. 링버퍼 — 고정 메모리로 최근 N개를 들고 있기

> **이 노트를 읽고 나면**
> - 고정 배열 하나와 인덱스 두 개로 "최근 N개" 저장소를 빈 종이에서 15분 안에 쓸 수 있다
> - `% cap` / `& (cap-1)` / 자유 증가 인덱스를 구분해서 고르고, 그 이유를 말할 수 있다
> - 창(window) 기반 eviction을 하면서 **창 경계 바로 앞 샘플 하나를 남겨야 하는 이유**를 설명할 수 있다
>
> **선행**: 없음 (C 배열과 `unsigned` 산술만 안다고 가정)
>
> **이 개념을 쓰는 문제**: [01_gps_fix_cache](../01_gps_fix_cache/question_note) Part 2의 fix 히스토리,
> [05_frame_latest_and_replay](../05_frame_latest_and_replay/question_note) Part 2의 replay 링,
> [08_battery_energy_pipeline](../08_battery_energy_pipeline/question_note) Part 2의 30분 히스토리와
> stage 1 → stage 2 bounded queue, 그리고 원본 ALS 문제의 `get_lux_at()` 저장소.

---

## 1. 왜 이게 필요한가

원본 Verkada 문제의 Part 2는 이렇게 생겼다.

> "지난 **10분** 안의 아무 시각 t에 대해 그 시각의 lux를 반환하라."

막히는 지점은 알고리즘이 아니라 **어디에 담을지**다. 화이트보드에서 제일 먼저 쓰는 것은 `malloc`
으로 노드를 만드는 연결 리스트다. 그런데 이 코드는 장비 안에서 **몇 달 동안 재부팅 없이** 돈다.

- `malloc`/`free`를 초당 10번씩 몇 달 하면 **힙이 조각난다**. 어느 날 4 KB 할당이 실패하는데,
  그때가 현장에 나가 있는 장비 안이다.
- `malloc`은 **얼마나 걸릴지 보장이 없다**. 샘플러 스레드가 거기서 멈추면 그 사이에 온 센서 보고는
  사라진다 — 파일과 달리 센서는 다시 읽을 수 없다.
- 메모리 사용량이 **위에서 안 막힌다**. 데이터가 예상보다 빨리 들어오면 OOM killer가 죽인다.

그래서 임베디드에서 "최근 N개"는 거의 항상 **한 번 잡은 고정 배열 + 인덱스 두 개** — 링버퍼(ring
buffer = circular buffer)다. 01번 모범답안 `gps_cache_solution.c`가 그 결정을 이렇게 적었다.

```
 * Fixed-size ring: bounded memory on an embedded box, O(1) append and evict,
 * and timestamps are non-decreasing so it is a sorted array -> binary search
 * gives O(log n) for "fix in effect at t".
```

한 줄로: **메모리가 위에서 막히고, 추가·삭제가 O(1)이고, 정렬된 배열이라 이진 탐색이 된다.**
마지막 항목이 [C2](C2_sorted_series_and_binary_search.md)로 이어진다.

## 2. 그림으로 먼저

메모리에서 링버퍼는 원이 아니라 **일직선 배열**이다. "원"은 인덱스를 다루는 방식일 뿐이다.

```svg
<svg viewBox="0 0 600 150" role="img" aria-label="고정 배열과 head, tail 인덱스">
  <text x="8" y="18">buf[8] — 메모리에서는 그냥 일직선. 채워진 칸이 살아 있는 데이터.</text><rect class="box" x="8" y="34" width="68" height="44" rx="6"/><rect class="box" x="80" y="34" width="68" height="44" rx="6"/><rect class="fill-soft" x="152" y="34" width="68" height="44" rx="6"/><rect class="fill-soft" x="224" y="34" width="68" height="44" rx="6"/><rect class="fill-soft" x="296" y="34" width="68" height="44" rx="6"/><rect class="fill-soft" x="368" y="34" width="68" height="44" rx="6"/><rect class="box" x="440" y="34" width="68" height="44" rx="6"/><rect class="box" x="512" y="34" width="68" height="44" rx="6"/>
  <text x="186" y="62" text-anchor="middle">t=40</text><text x="258" y="62" text-anchor="middle">t=90</text><text x="330" y="62" text-anchor="middle">t=140</text><text x="402" y="62" text-anchor="middle">t=190</text>
  <text class="lbl" x="42" y="94" text-anchor="middle">0</text><text class="lbl" x="114" y="94" text-anchor="middle">1</text><text class="lbl" x="186" y="94" text-anchor="middle">2</text><text class="lbl" x="258" y="94" text-anchor="middle">3</text><text class="lbl" x="330" y="94" text-anchor="middle">4</text><text class="lbl" x="402" y="94" text-anchor="middle">5</text><text class="lbl" x="474" y="94" text-anchor="middle">6</text><text class="lbl" x="546" y="94" text-anchor="middle">7</text><line class="accent" x1="186" y1="118" x2="186" y2="84"/><polygon class="accent" points="186,80 181,92 191,92"/><text class="lbl" x="186" y="134" text-anchor="middle">tail (가장 오래된 것)</text>
  <line class="accent" x1="474" y1="118" x2="474" y2="84"/><polygon class="accent" points="474,80 469,92 479,92"/><text class="lbl" x="474" y="134" text-anchor="middle">head (다음에 쓸 칸)</text>
</svg>
```

`head`가 7을 지나면 0으로 돌아온다. 그 순간만 원처럼 보인다.

```svg
<svg viewBox="0 0 560 250" role="img" aria-label="배열이 원형으로 감기고 head가 tail을 쫓아간다">
  <circle class="box" cx="150" cy="125" r="88"/><text class="lbl" x="150" y="22" text-anchor="middle">0</text><text class="lbl" x="212" y="48" text-anchor="middle">1</text><text class="lbl" x="250" y="129" text-anchor="middle">2</text><text class="lbl" x="212" y="208" text-anchor="middle">3</text><text class="lbl" x="150" y="238" text-anchor="middle">4</text><text class="lbl" x="86" y="208" text-anchor="middle">5</text><text class="lbl" x="44" y="129" text-anchor="middle">6</text><text class="lbl" x="86" y="48" text-anchor="middle">7</text>
  <line class="accent" x1="150" y1="125" x2="228" y2="125"/><polygon class="accent" points="236,125 224,120 224,130"/><text class="lbl" x="196" y="115" text-anchor="middle">tail</text><line class="muted" x1="150" y1="125" x2="96" y2="71"/><polygon class="muted" points="89,64 100,66 97,77"/><text class="lbl" x="120" y="86" text-anchor="middle">head</text><path class="dash" d="M 238 125 A 88 88 0 1 1 94 69" fill="none"/><text x="300" y="60">head는 시계 방향으로만 간다.</text><text x="300" y="86">점선이 "살아 있는" 구간.</text><text x="300" y="112">head가 한 바퀴 돌아 tail을 만나면</text><text x="300" y="138">full — 여기서 두 갈래로 갈린다.</text>
  <text x="300" y="172">(a) 막는다 (bounded)</text><text x="300" y="198">(b) tail을 밀고 덮어쓴다</text><text x="300" y="222">     (drop-oldest)</text>
</svg>
```

## 3. 개념 (용어를 하나씩)

### head / tail

- `head` = **다음에 쓸 칸**("가장 최근 것의 다음"), `tail` = **가장 오래된 살아 있는 칸**.
- 개수 `count = head - tail` (자유 증가 인덱스일 때) 또는 별도 필드.

이름은 구현마다 반대로 쓰기도 한다(`front`/`back`, `rd`/`wr`). **면접에서는 어느 쪽이 "다음에 쓸
칸"인지 말로 한 번 못을 박고 시작한다** — off-by-one 논쟁이 반으로 줄어든다.

### wrap / drop-oldest vs bounded / eviction

- **wrap(감기)** = 인덱스가 `cap`에 닿으면 0으로 되돌리는 것. 방법이 세 가지다(§4).
- **drop-oldest vs bounded** = 꽉 찼을 때 오래된 것을 버릴지 새것을 거절할지. 같은 배열,
  **다른 자료구조**다. 어느 쪽인지가 지문에 반드시 들어 있다(§5).
- **eviction** = **시간이 지나서** 필요 없어진 항목을 버리는 것. 용량이 남아도 내보낸다. 함정이 있다(§5).

### capacity — 산수로 정하는 것

배열 칸 수. **컴파일 타임에 정한다.** 실행 중에 안 변하므로 이 산수가 설계의 절반이다.

```
필요한 칸 수 = 레이트(초당 개수) × 보관 시간(초),   필요한 바이트 = 칸 수 × sizeof(한 칸)
용량        = 그 칸 수를 넘는 가장 가까운 2의 거듭제곱 (마스크를 쓰려면)
```

**이 산수를 소리 내어 하는 것 자체가 점수다.** "링버퍼를 쓰겠습니다"보다 "초당 10개 × 600초 =
6000개, 48바이트씩 288 KB, 8192개로 올리면 384 KB, 괜찮습니다"가 훨씬 강하다. 세 번 해 본다.

**(a) 원본 ALS — 10분.** `read_next_sample()`은 최대 1초 블로킹, 변화가 있을 때만 VALID → **초당
최대 1개**. `struct SensorReading` = `int` 4 + `float` 4 + `uint64_t` 8 = 16 B.

```
1/s × 600 s = 600 개,   600 × 16 B = 9,600 B ≈ 9.4 KiB,   올리면 1024 개 = 16 KiB
```

16 KiB. 카메라 SoC에서 아무것도 아니다. **여기서 "그럼 연결 리스트?"가 사라진다.**

**(b) 05번 프레임 replay — 2초.** `FRAME_BYTES = 4096`, `CAPTURE_PERIOD_US = 25000`(40 fps),
창 2초. 예산은 `FRAME_REPLAY_BYTES_MAX = 768 KiB`로 헤더에 **주어져 있다**.

```
40/s × 2 s = 80 프레임,  한 칸 = 4096 B + 16 B = 4112 B
80 × 4112 B = 328,960 B ≈ 321 KiB          (예산 안)
올려서 128 개 × 4112 B = 514 KiB           (예산 768 KiB 안)
256 개로 올리면 1,028 KiB → 예산 초과. 못 쓴다.
```

그래서 05번은 `REPLAY_CAP = 128`이다. **예산이 주어진 문제는 2의 거듭제곱으로 올린 뒤 예산을
다시 확인해야 한다.** (a)와 다른 점이다.

**(c) 08번 에너지 히스토리 — 30분.** 실제 BMS는 약 1초마다지만 하네스는 20~60 ms다.
**최악을 잡아 20 ms**로 산정한다.

```
1 / 0.020 s = 50/s,  30분 = 1800 s  ->  50 × 1800 = 90,000 개
한 칸 = uint64 ts + double p + double cum = 24 B  ->  2,160,000 B ≈ 2.1 MiB
올려서 2^17 = 131,072 개 × 24 B = 3.1 MiB
```

08번 모범답안 `energy_store_solution.c`의 주석이 정확히 이 계산이다.

```
 * 1800 s / 0.020 s = 90 000 entries. Round up to a power of two so the ring
 * index is a mask instead of a modulo: 2^17 = 131 072 entries * 24 B = 3.1 MiB
 * of BSS, allocated once at start-up. No malloc on the sampling path.
```

3.1 MiB는 BSS에 들어간다 — **실행 파일이 커지는 게 아니라** 시작할 때 0으로 채워진 페이지다.
MCU라면 SRAM 예산과 싸워야 하고, 그때는 창을 줄이거나 다운샘플링하거나 한 칸을 작게 만든다
(`double cum` → `int64_t` 밀리 단위, [C3](C3_prefix_sum_and_range_query.md)).

## 4. 코드로 보기

### (1) `% cap` — 가장 읽기 쉬운 방식

```c
#define CAP 4u
typedef struct {
    int      buf[CAP];
    unsigned head;      /* 다음에 쓸 칸 */
    unsigned tail;      /* 가장 오래된 칸 */
    unsigned count;
} ring_t;

static bool ring_push(ring_t *r, int v)
{
    if (r->count == CAP) return false;          /* full */
    r->buf[r->head] = v;
    r->head = (r->head + 1u) % CAP;
    r->count++;
    return true;
}
/* pop 은 대칭이다: count == 0 이면 empty, 아니면 buf[tail] 을 읽고 tail 을 한 칸 민다. */
```

- `if (r->count == CAP) return false;` — **이 줄이 없으면?** `head`가 `tail`을 넘어 안 읽은 데이터를
  조용히 덮어쓰고 `count`가 `CAP`을 넘어 자료구조가 망가진다.
- `% CAP`은 나눗셈이다. Cortex-M0처럼 하드웨어 나눗셈이 없으면 수십 사이클. 초당 10번이면 문제
  없고, 초당 100만 번이면 (2)로 간다. `count`는 full/empty 구분용이다(§5).

### (2) `& (cap - 1)` — 2의 거듭제곱 용량일 때

```c
#define FCAP  8u          /* 반드시 2의 거듭제곱 */
#define FMASK (FCAP - 1u)
_Static_assert((FCAP & FMASK) == 0u, "FCAP must be a power of two");
```

`x % 8`과 `x & 7`은 **x가 unsigned이고 8이 2의 거듭제곱일 때만** 같다. `FCAP = 10`에 `& 9`를 쓰면
인덱스 9는 `9 & 9 = 9`, 10은 `10 & 9 = 8`이 되어 칸을 건너뛰고 겹친다 — 조용히 틀린다.

`01_gps_fix_cache/gps_cache_solution.c`의 그 한 줄:
`_Static_assert((GPS_HIST_CAP & (GPS_HIST_CAP - 1u)) == 0u, "...power of two");`

### (3) 자유 증가 인덱스(free-running) — 면접에서 쓸 기본값

`head`/`tail`을 **절대 감싸지 않고** 계속 증가시킨다. 배열에 넣을 때만 마스크를 씌운다.

```c
typedef struct {
    int      buf[FCAP];
    uint32_t head;   /* 감싸지 않는다. 계속 증가 */
    uint32_t tail;
} fring_t;

static uint32_t fring_count(const fring_t *r) { return r->head - r->tail; }
static bool     fring_empty(const fring_t *r) { return r->head == r->tail; }
static bool     fring_full (const fring_t *r) { return r->head - r->tail == FCAP; }

static bool fring_push_bounded(fring_t *r, int v)   /* 막는 링 */
{
    if (fring_full(r)) return false;            /* 새것을 거절한다 */
    r->buf[r->head & FMASK] = v; r->head++;
    return true;
}

static void fring_push_overwrite(fring_t *r, int v) /* 덮어쓰는 링 */
{
    if (fring_full(r)) r->tail++;               /* drop-oldest: 오래된 것을 버린다 */
    r->buf[r->head & FMASK] = v; r->head++;
}
```

좋은 점: `count` 필드가 필요 없고, full/empty가 자동으로 구분되고, **인덱스 자체가 "몇 번째
샘플이냐"는 절대 번호**가 된다 — [C3](C3_prefix_sum_and_range_query.md)의 절대 누적합과 같은 발상.

**오버플로는 안전한가?** `uint32_t`는 2^32에서 0으로 돌아간다. 그런데 우리가 읽는 값은 언제나
`head - tail`이라는 **차이**다. C에서 unsigned 산술은 2^32에 대한 나머지 연산으로 **정의되어
있다**(signed와 달리 UB가 아니다). 그래서 `head`가 감겨도 차이는 맞다. 실제 출력:

```
== 인덱스 오버플로: head 가 uint32 끝을 넘어간다 ==
push 1 -> head=4294967295 tail=4294967294 count=1
push 2 -> head=         0 tail=4294967294 count=2
push 3 -> head=         1 tail=4294967294 count=3
push 4 -> head=         2 tail=4294967294 count=4
push 5 -> head=         3 tail=4294967294 count=5
내용: 1 2 3 4 5
```

**단 조건이 있다**: 두 인덱스가 unsigned여야 하고 `head - tail`이 `FCAP`(그리고 2^31)을 넘지 않아야
한다. `int`면 signed overflow = **UB**이고 `-O2`에서 컴파일러가 비교문을 지울 수 있다(§6 첫 줄).

| 방식 | 비용 | 용량 제약 | full/empty | 오버플로 안전성 |
| --- | --- | --- | --- | --- |
| `% cap` | 나눗셈 (수십 사이클 가능) | 아무 값 | `count` 필드 필요 | 인덱스가 `cap` 미만이라 문제 없음 |
| `& (cap-1)` | AND 1개 | **2의 거듭제곱만** | `count` 필드 필요 | 문제 없음 |
| 자유 증가 + 마스크 | AND 1개 | 2의 거듭제곱만 | 자동 (`head - tail`) | unsigned면 정의된 동작, signed면 UB |

면접에서는 **자유 증가 + 마스크**를 기본값으로 쓰고, "2의 거듭제곱이 아니어야 하면 `% cap` +
`count` 필드"라고 한 줄 덧붙이면 충분하다.

## 5. 단계별로 만들어 보기

### v0: full과 empty가 구별되지 않는다

```
CAP = 4        비었을 때            꽉 찼을 때
               [ . . . . ]          [ a b c d ]
                 ^                    ^
                 head=tail=0          head=0, tail=0   ← 구별이 안 된다!
```

상태는 `cap+1`가지인데 구분 가능한 조합이 `cap`가지뿐이다. **한 비트가 부족하다.** 해결책이 셋이다.

**(1) `count` 필드** (§4의 1) — 용량을 다 쓰고 제일 읽기 쉽다. 변수가 셋이라 lock-free는 어렵다.

**(2) 한 칸 비우기** — `head == tail`은 empty, `(head+1) % cap == tail`은 full.

```c
static bool ring2_empty(const ring2_t *r) { return r->head == r->tail; }
static bool ring2_full (const ring2_t *r) { return (r->head + 1u) % CAP == r->tail; }
```

변수 둘이라 SPSC lock-free 큐의 고전적 모양이다. 대신 **`CAP`칸을 잡아도 `CAP-1`개만 들어간다**.

**(3) 자유 증가 인덱스의 차이** (§4의 3) — 변수 둘, 용량 전부, 구분 자동. 01번·08번의 방식.

### v1: 덮어쓰기 링과 막는 링을 갈라 놓는다

같은 배열, **요구는 정반대**다.

```
drop-oldest (덮어쓰기)               bounded (막기)
  push는 절대 실패하지 않는다          push가 실패할 수 있다 (또는 기다린다)
  오래된 데이터를 잃는다                새 데이터를 잃거나, 생산자가 멈춘다
  "최근 N개 히스토리" — 01, 05, 08     "작업 큐" — 08의 stage 1 → stage 2
```

08번이 왜 drop-oldest를 골랐는지 설명한 대목이 이 결정의 교과서다.

```
 *    would not be sitting in bms_read() when the BMS reports, and the reading
 *    is simply gone: unlike a file, a sensor cannot be re-read later. So the
 *    hand-off from stage 1 to stage 2 is a BOUNDED queue with a DROP-OLDEST
 *    policy: pushing is O(1), never waits, and can never fail.
```

**센서를 읽는 스레드는 절대 기다리게 하면 안 된다.** 기다리는 동안 온 보고는 영원히 사라진다.
그래서 `push`는 실패할 수 없어야 하고, 남은 선택은 오래된 것을 버리는 것뿐이다. 반대로 "출입
카드 이벤트를 하나도 잃으면 안 된다"면 막는 링이 맞다. `FCAP=8`에 1~10을 넣어 보면:

```
== v2: 자유 증가 인덱스, 막는 링 ==
push  8 -> ok   count=8
push  9 -> FULL count=8
push 10 -> FULL count=8

== v2: 자유 증가 인덱스, 덮어쓰는 링 ==
count=8  내용(오래된 것부터): 3 4 5 6 7 8 9 10
head=10 tail=2  (둘 다 8을 넘었지만 차이는 8)
```

### v2 (틀림): 창 밖을 전부 버리는 eviction

```c
/* 틀렸다 */
while (n >= 1 && hist_at(h, 0)->ts <= now - window) { h->tail++; n--; }
```

논리적으로 맞아 보인다. 그런데 **원본 문제의 정의를 다시 읽어야** 한다.

> 시각 t의 값 = `timestamp <= t`인 가장 최근 VALID 샘플의 값.

센서는 **값이 변할 때만** 보고한다. 그래서 t가 창의 왼쪽 끝(`now - window`)일 때 답이 되는 샘플은
**창이 열리기 전에 찍힌 샘플**일 수 있다. 버리면 창 왼쪽 끝 질의가 "데이터 없음"을 반환한다.

```svg
<svg viewBox="0 0 620 210" role="img" aria-label="창 경계 앞 샘플 하나를 남겨야 하는 이유">
  <line class="muted" x1="30" y1="120" x2="590" y2="120"/><polygon class="muted" points="598,120 586,115 586,125"/><text class="lbl" x="576" y="142">시간</text><rect class="fill-soft" x="300" y="60" width="270" height="60" rx="6"/><text class="lbl" x="435" y="50" text-anchor="middle">보관하기로 약속한 창 (지난 window)</text><line class="dash" x1="300" y1="40" x2="300" y2="150"/><text class="lbl" x="300" y="166" text-anchor="middle">now - window</text><line class="dash" x1="570" y1="40" x2="570" y2="150"/><text class="lbl" x="570" y="166" text-anchor="middle">now</text><circle class="accent" cx="80" cy="120" r="6"/><text class="lbl" x="80" y="104" text-anchor="middle">t=40</text>
  <circle class="accent" cx="160" cy="120" r="6"/><text class="lbl" x="160" y="104" text-anchor="middle">t=90</text><circle class="accent" cx="250" cy="120" r="8"/><text class="lbl" x="250" y="104" text-anchor="middle">t=140</text><circle class="accent" cx="400" cy="120" r="6"/><text class="lbl" x="400" y="104" text-anchor="middle">t=190</text><circle class="accent" cx="540" cy="120" r="6"/><text class="lbl" x="540" y="104" text-anchor="middle">t=240</text><line class="accent" x1="250" y1="192" x2="250" y2="136"/><polygon class="accent" points="250,130 245,142 255,142"/><text x="266" y="198">이 하나는 창 밖이지만 버리면 안 된다.</text><text class="lbl" x="266" y="178">t=150 을 물으면 답이 바로 이것이다.</text>
</svg>
```

### v3 (고침): 0번은 1번도 창 밖일 때만 버린다

```c
static void hist_append(hist_t *h, uint64_t now, uint64_t window, uint64_t ts, int v)
{
    uint32_t n = h->head - h->tail;
    /* 0번을 버리는 조건은 "1번도 창 밖"일 때뿐 — 경계 직전 샘플 하나는 남긴다 */
    while (n >= 2 && hist_at(h, 1)->ts <= now - window) { h->tail++; n--; }
    if (n == HCAP) { h->tail++; n--; }          /* 그래도 꽉 차면 어쩔 수 없이 버린다 */
    h->buf[h->head & HMASK] = (sample_t){ ts, v };
    h->head++;
}
```

실행 결과 (창 = 100, now = 250 → 창은 `[150, 250]`):

```
== 시간 기반 eviction: 창 = 100, now = 250 ==
남은 샘플 (창은 [150, 250]): (t=140,v=30) (t=190,v=40) (t=240,v=50)
  -> t=140 은 창 밖이지만 살아 있다. t=150 의 값을 물으면 답이 이것이다.
```

01번도 같은 조건이고, 주석이 그 이유다.

```
 * Eviction keeps ONE fix older than the window: the position in effect at
 * (now - window) comes from a fix taken before the window opened.
```

08번은 보간 때문에 더 강하게 필요하다 — 창 왼쪽 끝의 전력을 앞뒤 샘플 사이에서 보간하므로
앞 샘플이 없으면 가장 오래된 구간이 조용히 잘려 나간다.

`n >= 2`가 없으면? `tail`이 `head`를 지나가 `head - tail`이 거대한 unsigned 값이 되어 폭발한다.
**이 하한이 unsigned 산술의 유일한 위험 지점이다.**

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
| --- | --- | --- | --- |
| `head`/`tail`을 `int`로 선언 | `-O0`에서는 되는데 `-O2`에서만 깨진다. 오래 돌리면 인덱스가 음수가 되어 배열 밖을 읽는다 | signed overflow는 UB. 컴파일러가 "오버플로 없음"을 가정해 비교문을 지운다 | `uint32_t` / `unsigned`로. 차이만 읽는다 |
| 2의 거듭제곱이 아닌 용량에 `& (cap-1)` | 일부 칸을 건너뛰고 일부를 두 번 쓴다. 데이터가 뒤섞이지만 크래시는 안 난다 | `x % 10`과 `x & 9`는 다르다. 마스크는 2의 거듭제곱에서만 modulo와 같다 | `_Static_assert((CAP & (CAP-1)) == 0)`, 아니면 `% CAP` |
| `count` 없이 `head == tail`로 full/empty 둘 다 판단 | 꽉 찬 링이 비어 있다고 보고된다. 또는 push가 조용히 데이터를 덮어쓴다 | 상태가 `cap+1`가지인데 구분 가능한 조합이 `cap`가지뿐 | count 필드 / 한 칸 비우기 / 자유 증가 인덱스 중 하나 |
| eviction에서 창 밖을 전부 버린다 | 창 가운데 질의는 맞는데 **왼쪽 끝 근처만** "데이터 없음". 하네스의 경계 검사에서만 FAIL | 창 시작 시각의 값은 창 전에 찍힌 샘플이 들고 있다 | 0번을 버리는 조건을 "1번도 창 밖"으로 (§5 v3) |
| eviction 루프에 `n >= 2` 하한이 없다 | `tail`이 `head`를 넘어 `count`가 40억이 된다. 다음 질의에서 즉사 | unsigned 뺄셈은 음수가 아니라 wrap한다 | 루프 조건에 개수 하한을 반드시 넣는다 |
| `cap`이 0이거나 배열 길이와 다른 매크로 | 첫 push에서 배열 밖 쓰기. ASan 없이는 원인을 못 찾는다 | `% 0`은 UB, 마스크는 0이 되어 항상 `buf[0]` | `_Static_assert(CAP >= 2)`와 배열을 같은 매크로로 선언 |

## 7. 손으로 확인하기

01번의 eviction 경로를 직접 본다. 창을 1.5초로 줄여 빌드하는 모드가 있다.

```sh
cd 01_gps_fix_cache && ./main.sh sol       # 10분 창 — eviction이 안 일어난다
cd 01_gps_fix_cache && ./main.sh window sol  # 1.5초 창 — eviction이 돌고 경계 질의를 검사한다
```

그다음 `gps_cache.c`(stub)를 채우면서 **§5의 v2(창 밖을 전부 버리는 버전)로 일부러 써 보고**
`./main.sh window`를 돌린다. 경계 검사만 FAIL이 뜨는 것을 눈으로 보는 것이 가장 빠른 학습이다.

```sh
cd 08_battery_energy_pipeline && grep -n "HIST_CAP\|Capacity" energy_store_solution.c && ./main.sh sol
cd 05_frame_latest_and_replay && grep -n "REPLAY_CAP\|_Static_assert" frame_store_solution.c && ./main.sh sol
```

이 노트의 코드 조각은 한 파일로 모아 실제로 돌려 본 것이다. §4·§5를 한 `.c`로 붙이고
`cc -std=c11 -O2 -Wall -Wextra -o c1 c1.c && ./c1`.

## 8. 자가 점검

```check
Q: 임베디드에서 "최근 10분"을 연결 리스트가 아니라 고정 배열 링버퍼로 담는 이유를 세 가지 말해 보라.
A: (1) 메모리가 위에서 막힌다 — 데이터가 예상보다 빨리 들어와도 OOM이 나지 않는다.
   (2) 샘플링 경로에 malloc이 없다 — 할당 지연이나 몇 달 뒤의 힙 조각화로 센서 읽기를 놓치지 않는다.
   (3) 타임스탬프가 비감소이므로 정렬된 배열이 되어 이진 탐색으로 O(log n) 질의가 된다.
   연결 리스트는 노드마다 포인터 오버헤드가 붙고 O(n) 탐색만 된다.

Q: head와 tail을 uint32_t로 두고 절대 감싸지 않는 방식이 오버플로에 안전한 이유는?
A: 코드가 읽는 값은 언제나 head - tail이라는 차이뿐이다. C에서 unsigned 산술은 2^32에 대한
   나머지 연산으로 정의되어 있어서(signed와 달리 UB가 아니다) head가 0으로 감겨도 차이는 맞다.
   조건은 두 인덱스가 unsigned이고, 차이가 CAP(그리고 2^31)을 넘지 않는 것이다.
   int로 쓰면 signed overflow = UB라서 -O2에서 조용히 깨진다.

Q: full과 empty를 구분하는 세 방법과 각각의 대가는?
A: (1) count 필드 — 용량을 다 쓰고 제일 읽기 쉽지만 변수가 셋이라 lock-free로 만들기 어렵다.
   (2) 한 칸 비우기 — 변수 둘로 SPSC lock-free가 되지만 cap칸을 잡고 cap-1개만 쓴다.
   (3) 자유 증가 인덱스의 차이 — 변수 둘, 용량 전부, 구분 자동. 01번과 08번이 이 방식이다.

Q: drop-oldest 링과 bounded 링 중 무엇을 고를지 어떻게 판단하나? 08번은 왜 drop-oldest인가?
A: "잃어도 되는 것이 오래된 데이터냐, 새 데이터냐"로 결정한다. 08번의 stage 1 → stage 2
   hand-off는 drop-oldest다. 센서를 읽는 스레드가 큐 때문에 잠깐이라도 멈추면 그 사이에 BMS가
   보고한 값은 영원히 사라진다 — 파일과 달리 센서는 다시 읽을 수 없다. 그래서 push가 절대
   실패하지도 기다리지도 않아야 하고, 남은 선택은 오래된 것을 버리는 것뿐이다. 업로드는
   best-effort이고 30분 로컬 히스토리가 진짜 원본이다.

Q: 창 기반 eviction에서 "창보다 오래된 샘플 하나"를 왜 남기는가? 안 남기면 어떤 증상인가?
A: 센서는 값이 변할 때만 보고하므로, 창의 왼쪽 끝 시각의 값은 창이 열리기 전에 찍힌 샘플이
   들고 있다. 그것을 버리면 창 가운데 질의는 다 맞는데 왼쪽 끝 근처만 "데이터 없음"을 반환한다.
   경계 검사에서만 FAIL이 나는 조용한 버그다. 조건은 "0번은 1번도 창 밖일 때만 버린다".
   08번은 보간까지 하므로 앞 샘플 없이는 가장 오래된 구간이 잘려 나간다.

Q: 원본 ALS 문제(10분 창)의 링 용량을 숫자로 산정해 보라. 05번에서는 한 단계를 더 확인해야 하는데 무엇인가?
A: ALS는 초당 최대 1개이므로 1/s × 600 s = 600개, struct가 16 B이니 9.6 KB, 2의 거듭제곱으로
   올리면 1024개 = 16 KiB. 05번은 메모리 예산이 헤더에 768 KiB로 주어져 있어서, 80프레임 ×
   4112 B = 321 KiB를 128개(514 KiB)로 올린 뒤 예산 안인지 다시 확인해야 한다. 256개면
   1,028 KiB로 초과다. 모범답안은 그 확인을 _Static_assert로 고정해 둔다.
```

## 9. 요약 카드

- 링버퍼 = **고정 배열 + `head`(다음에 쓸 칸) + `tail`(가장 오래된 칸)**. 원이 아니라 인덱스 규칙이다.
- 인덱스는 **`uint32_t` 자유 증가 + `& (cap-1)`**을 기본값으로. `count = head - tail`.
  `int`는 금지(UB), 마스크는 2의 거듭제곱에서만.
- full/empty 구분: **count 필드 / 한 칸 비우기 / 자유 증가 차이** 중 하나. 아무것도 안 하면 못 구분한다.
- 꽉 찼을 때: **drop-oldest**(센서 히스토리, 절대 실패 안 함) vs **bounded**(작업 큐, 실패 가능).
  센서 읽는 스레드는 절대 기다리지 않게 한다.
- 용량 = **레이트 × 시간 × 한 칸 크기**, 2의 거듭제곱으로 올리고 **예산이 있으면 다시 확인**.
- 창 eviction: **0번은 1번도 창 밖일 때만 버린다.** 경계의 답은 창 전 샘플이 들고 있다.
- eviction 루프에는 개수 하한(`n >= 2`)을 반드시. 없으면 `tail`이 `head`를 넘어 40억이 된다.
- 비감소 타임스탬프 링 = **정렬된 배열** → [C2. 이진 탐색](C2_sorted_series_and_binary_search.md).
