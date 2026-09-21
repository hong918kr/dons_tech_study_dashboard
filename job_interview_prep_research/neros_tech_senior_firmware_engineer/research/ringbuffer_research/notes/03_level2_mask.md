# 🎭 레벨 2 — 마스킹과 free-running 인덱스

> **이 노트가 이 시리즈의 중심이다.** 레벨 0의 낭비, 레벨 0의 나눗셈, 레벨 1의 경합 —
> 세 문제를 **아이디어 두 개**로 한꺼번에 없앤다. 실무 펌웨어 링버퍼의 90%가 이 형태다.
> 이 레벨을 손으로 두 번 짜면 나머지 레벨은 전부 여기에 뭘 덧붙이는 이야기다.
>
> 연습: `make prob N=L2_mask`

---

## 1. 아이디어 두 개

### 아이디어 ①: `size` 를 2의 거듭제곱으로 강제 → `%` 를 `& mask` 로

```
 size = 8 = 0b1000
 mask = 7 = 0b0111

 idx % 8   ==   idx & 7      (idx 가 unsigned 일 때 항상 참)

  13 % 8 = 5        13 & 7 = 0b1101 & 0b0111 = 0b0101 = 5  ✅
```

나눗셈 수십 사이클 → **AND 1사이클.** 분기도 없다. 실행 시간이 완전히 결정적(deterministic)이다.

### 아이디어 ②: 인덱스를 마스킹해서 **저장하지 않는다** (free-running)

레벨 0/1은 인덱스를 `0 ~ size-1` 안에 가둬 놓았다. 레벨 2는 **그냥 계속 증가시킨다.**
마스킹은 **버퍼에 접근할 때만** 한다.

```c
q->buf[q->head & q->mask] = v;   /* 접근할 때 마스킹 */
q->head++;                       /* 인덱스 자체는 그냥 증가 */
```

그러면 이렇게 된다.

```c
uint32_t rb2_used(const rb2_t *q) { return q->head - q->tail; }   /* ★ 이게 전부 */
uint32_t rb2_free(const rb2_t *q) { return q->size - rb2_used(q); }
bool     is_empty(const rb2_t *q) { return q->head == q->tail; }
bool     is_full (const rb2_t *q) { return q->head - q->tail == q->size; }
```

- `count` 필드가 **사라졌다** → 레벨 1의 write-write 경합이 사라진다.
- `head == tail` 은 이제 **오직 empty** 다. full 일 때는 `head - tail == size` 이므로 절대 같지 않다.
  → **한 칸을 버릴 필요가 없다.** 8칸이면 8개 전부 쓴다.

> 세 문제를 동시에 해결한다. 이래서 이 구조가 표준이 됐다.

---

## 2. 구조체와 초기화

```c
typedef struct {
    uint8_t *buf;
    uint32_t size;    /* 반드시 2의 거듭제곱 */
    uint32_t mask;    /* size - 1 */
    uint32_t head;    /* free-running */
    uint32_t tail;    /* free-running */
} rb2_t;

static bool rb2_is_pow2(uint32_t n) {
    return n != 0 && (n & (n - 1)) == 0;
}

bool rb2_init(rb2_t *q, uint8_t *buf, uint32_t size) {
    if (!rb2_is_pow2(size)) return false;   /* ★ 여기서 막지 않으면 조용히 깨진다 */
    q->buf = buf; q->size = size; q->mask = size - 1;
    q->head = q->tail = 0;
    return true;
}
```

### `n & (n-1)` 이 2의 거듭제곱 판별인 이유

```
  n     = 8 = 0b1000
  n - 1 = 7 = 0b0111
  n & (n-1) = 0b0000 = 0   ← 켜진 비트가 정확히 1개일 때만 0

  n     = 12 = 0b1100
  n - 1 = 11 = 0b1011
  n & (n-1) = 0b1000 ≠ 0   ← 2의 거듭제곱 아님
```

`n != 0` 체크를 빼먹으면 `0 & -1 == 0` 이라 **0이 2의 거듭제곱으로 통과**한다.
그러면 `mask = 0xFFFFFFFF` 가 되어 아무 주소나 쓴다. 전형적인 버그다.

---

## 3. ⭐⭐ 인덱스 오버플로 — "계속 증가시키면 터지지 않나?"

**면접 단골 질문이고, 답은 "터지지 않는다"이다.** 이유를 정확히 말할 수 있어야 한다.

`uint32_t` 는 `0xFFFFFFFF` 다음에 `0` 으로 랩한다. 그런데 `used = head - tail` 은 **여전히 맞는다.**

```
  size = 8
  head = 0xFFFFFFFE,  tail = 0xFFFFFFFC   →  used = 2   ✅

  3바이트 더 쓰면 head = 0x00000001 (랩!)
  used = 0x00000001 - 0xFFFFFFFC
       = mod 2^32 로 계산하면 5                           ✅  정확하다
```

### 왜 맞는가 — C 표준의 근거

> **부호 없는 정수 연산은 `2^N` 을 법(modulo)으로 하는 산술이다** (C11 §6.2.5p9).
> 오버플로라는 개념 자체가 없다. 정의된 동작이다.

`head - tail` 은 실제로는 "두 free-running 카운터의 mod 2^32 차이"이고, 이는
**실제로 넣은 총 개수 − 실제로 뺀 총 개수**와 항상 같다. `used <= size <= 2^31` 인 한
이 차이는 절대 모호해지지 않는다.

> ⚠️ **`int` 로 하면 UB.** 부호 있는 정수 오버플로는 C 에서 undefined behavior 이고,
> 컴파일러는 "오버플로는 일어나지 않는다"고 가정해 최적화한다. `-O2` 에서 조용히
> 이상한 코드가 나온다. **인덱스는 반드시 `unsigned`.**

### 실전 감각

1Mbps UART 로 초당 125,000바이트를 받아도 `uint32_t` 가 한 바퀴 도는 데 **9.5시간**.
그리고 한 바퀴 돌아도 **아무 일도 안 일어난다.** 굳이 걱정된다면 `uint64_t` 를 쓰면 되지만,
32비트 MCU 에서 64비트 store 는 **원자적이지 않아서** 레벨 4에서 더 큰 문제를 만든다.

---

## 4. 벌크 쓰기 — 2조각 `memcpy`

```c
uint32_t rb2_write(rb2_t *q, const uint8_t *src, uint32_t n) {
    uint32_t space = rb2_free(q);
    if (n > space) n = space;                   /* 부분 성공 */

    uint32_t off   = q->head & q->mask;         /* 버퍼 안에서의 실제 위치 */
    uint32_t first = q->size - off;             /* 거기서 배열 끝까지 */
    if (first > n) first = n;

    memcpy(q->buf + off, src, first);
    memcpy(q->buf,       src + first, n - first);   /* n==first 면 0바이트 복사 */

    q->head += n;                               /* ★ 마스킹하지 않는다 */
    return n;
}
```

`off`, `first` 두 줄이 전부다. **랩이 일어나든 말든 같은 코드가 돈다** — 분기가 없다는 게
실시간성에 좋다(`memcpy(dst, src, 0)` 은 합법이고 보통 즉시 리턴).

---

## 5. `rb2_peek_contig` — 레벨 5b 의 예고편

```c
/* 지금 읽을 수 있는 "연속" 구간의 길이를 리턴하고 시작 주소를 준다.
   랩 지점에서는 배열 끝까지만 리턴한다 (뒤쪽 조각은 다음 호출에서). */
uint32_t rb2_peek_contig(const rb2_t *q, const uint8_t **ptr) {
    uint32_t used = rb2_used(q);
    uint32_t off  = q->tail & q->mask;
    uint32_t contig = q->size - off;
    if (contig > used) contig = used;
    *ptr = q->buf + off;
    return contig;
}
```

이 함수가 있으면 **복사 없이** DMA 에 넘길 수 있다.

```c
const uint8_t *p;
uint32_t n = rb2_peek_contig(&tx, &p);
if (n) HAL_UART_Transmit_DMA(&huart1, (uint8_t *)p, n);   /* memcpy 0회 */
```

`memcpy` 를 완전히 없애는 완성형이 [레벨 5b](06_level5_overwrite_zerocopy.html)다.

---

## 6. 대가: `size` 가 2의 거듭제곱이어야 한다

| 원하는 크기 | 써야 하는 크기 | 낭비 |
|---|---|---|
| 100 | 128 | 28% |
| 500 | 512 | 2.4% |
| 1000 | 1024 | 2.4% |
| 1500 (이더넷 MTU) | 2048 | 27% 😖 |
| 3000 | 4096 | 27% |

레벨 0이 버리는 건 **한 칸**이고, 레벨 2가 버리는 건 **최대 50% 가까이**다.
그런데도 레벨 2를 쓰는 이유는:

- MCU 의 RAM 배치상 어차피 2의 거듭제곱 경계가 편하다 (MPU 영역, 캐시라인, DMA 정렬)
- 나눗셈 제거 + 무락 가능성이 몇 백 바이트보다 훨씬 비싸다
- 버퍼 크기는 보통 **여유 있게 잡는 값**이지 정밀한 값이 아니다

> 정확한 크기가 꼭 필요하면(예: 정확히 10개짜리 명령 큐) **레벨 1**로 내려가는 게 맞다.
> "어느 레벨이 더 좋다"가 아니라 **제약에 맞는 레벨을 고르는 것**이 설계다.

### 2의 거듭제곱이 아니어도 free-running 을 쓸 수 있나?

가능하지만 `% size` 가 돌아오고, 더 나쁜 건 **인덱스가 `2^32` 에서 랩할 때 `2^32 % size != 0`
이면 위치 계산이 어긋난다**는 점이다. 그래서 free-running 은 사실상 2의 거듭제곱과 한 세트다.
(비-2^n 을 쓰려면 인덱스를 `0 ~ 2*size-1` 로 가두는 변형이 있다. 알아만 두면 된다.)

---

## 7. 레벨 0/1/2 총정리

| | L0 (`%`+한 칸) | L1 (`count`) | **L2 (mask+free-running)** |
|---|---|---|---|
| 8칸의 실제 용량 | 7 | 8 | **8** |
| 인덱스 전진 비용 | `%` (20~40cy on M0) | `%` | **`&` 1cy** |
| 공유 쓰기 변수 | 없음 | ⚠️ `count` | **없음** |
| ISR↔메인 무락 | 어려움 | ❌ | **✅ (레벨 4에서 완성)** |
| size 제약 | 없음 | 없음 | ⚠️ 2의 거듭제곱 |
| 읽기 쉬움 | ✅ | ✅✅ | 보통 (free-running 이 처음엔 낯설다) |

---

## 8. 면접 답변 템플릿

> *"버퍼 크기를 2의 거듭제곱으로 잡고 인덱스는 마스킹하지 않은 채 free-running 으로 증가시킵니다.
> 버퍼 접근할 때만 `& (size-1)` 을 합니다. 그러면 세 가지가 동시에 해결됩니다.
> 첫째, 나눗셈이 AND 한 번으로 줄어 Cortex-M0 처럼 하드웨어 나눗셈이 없는 코어에서도 결정적입니다.
> 둘째, `used = head - tail` 이라 count 필드가 필요 없고, 생산자는 head 만 소비자는 tail 만
> 쓰므로 write-write 경합이 구조적으로 없습니다 — 이게 lock-free SPSC 의 전제조건입니다.
> 셋째, `head == tail` 이 오직 empty 를 의미하므로 한 칸을 버릴 필요도 없습니다.
> 인덱스가 `uint32_t` 를 넘어 랩해도 부호 없는 뺄셈이 mod 2^32 라 `used` 는 정확합니다.
> 대가는 크기가 2의 거듭제곱으로 제한된다는 것입니다."*

---

## 9. 체크

- [ ] `n & (n-1)` 판별에서 `n != 0` 을 빼먹으면 뭐가 터지는지 안다
- [ ] `head = tail = 0xFFFFFFFC` 로 놓고 12바이트 write/read 해서 정확한지 **직접 테스트했다**
- [ ] `int` 인덱스가 왜 UB 인지 (signed overflow) 말할 수 있다
- [ ] `head == tail` 이 이 레벨에서는 오직 empty 인 이유를 설명할 수 있다
- [ ] 벌크 write 의 `off` / `first` 두 줄을 안 보고 짤 수 있다
- [ ] 1500바이트가 필요한데 2048을 잡아야 하는 상황에서 레벨 1로 내려갈 판단을 할 수 있다
- [ ] `rb2_peek_contig` 로 DMA 에 넘기는 코드를 세 줄로 쓸 수 있다
- [ ] 노트를 덮고 레벨 2를 **30분 안에** 처음부터 다시 짰다

다음: **[레벨 3 — 타입에 무관한 링버퍼](04_level3_generic.html)**
