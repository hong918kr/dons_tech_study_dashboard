# 🔢 레벨 1 — `count` 필드로 한 칸도 낭비하지 않기

> **목표**: 레벨 0이 버린 한 칸을 되찾는다. 그리고 그 대가로 **무엇을 잃었는지**를 정확히 안다.
> 이 레벨의 진짜 교훈은 "count 를 쓰면 편하다"가 아니라 **"count 는 두 주체가 공유하는 쓰기 가능한 상태다"** 이다.
>
> 연습: `make prob N=L1_count`

---

## 1. 구조체 — 필드 하나 추가

```c
#define RB1_CAP 8   /* 이제 8개 전부 저장 가능 */

typedef struct {
    int      buf[RB1_CAP];
    unsigned head;
    unsigned tail;
    unsigned count;   /* ★ 현재 들어있는 개수 */
} rb1_t;
```

모호함이 사라진다.

```c
bool rb1_is_empty(const rb1_t *q) { return q->count == 0; }
bool rb1_is_full (const rb1_t *q) { return q->count == RB1_CAP; }
```

`head == tail` 이어도 `count` 가 0이면 empty, `RB1_CAP` 이면 full. **끝.**
한 칸을 버릴 이유가 없어졌다.

```c
bool rb1_push(rb1_t *q, int v) {
    if (q->count == RB1_CAP) return false;
    q->buf[q->head] = v;
    q->head = (q->head + 1) % RB1_CAP;
    q->count++;                    /* ★ 생산자가 count 를 쓴다 */
    return true;
}

bool rb1_pop(rb1_t *q, int *out) {
    if (q->count == 0) return false;
    *out = q->buf[q->tail];
    q->tail = (q->tail + 1) % RB1_CAP;
    q->count--;                    /* ★ 소비자도 count 를 쓴다 */
    return true;
}
```

---

## 2. ⭐ 이 레벨의 핵심: `count++` 와 `count--` 가 같은 변수를 건드린다

레벨 0에서는 이랬다.

```
생산자가 쓰는 것: head        소비자가 쓰는 것: tail
생산자가 읽는 것: head, tail  소비자가 읽는 것: head, tail
                  → 서로의 변수는 읽기만 한다
```

레벨 1에서는 이렇게 된다.

```
생산자가 쓰는 것: head, count   ←┐
소비자가 쓰는 것: tail, count   ←┘  같은 변수를 둘 다 쓴다 (write-write 경합)
```

### 왜 치명적인가

`q->count++` 는 C 에서 한 줄이지만 기계어로는 **세 단계**다.

```asm
    LDR  r0, [q, #count]    ; 1. 읽고
    ADDS r0, r0, #1         ; 2. 더하고
    STR  r0, [q, #count]    ; 3. 쓴다      ← 이 사이에 인터럽트가 들어오면?
```

메인 루프가 `pop()` 안에서 `count` 를 읽어 `5` 를 얻은 직후, ISR 이 `push()` 를 두 번 해서
`count` 를 `7` 로 만들었다고 하자. 메인이 돌아와 `4` 를 쓴다. **ISR 이 넣은 2개가 증발한다.**

```
 시각   메인(pop)                 ISR(push ×2)            메모리의 count
 ────   ──────────────            ────────────            ──────────────
  t0    LDR r0 ← 5                                              5
  t1                              count = 6                     6
  t2                              count = 7                     7
  t3    ADDS/SUBS r0 = 4                                        7
  t4    STR r0 → count                                          4   ← 💥 2개 유실
```

이것이 고전적인 **read-modify-write 경쟁 조건**이다. 링버퍼는 조용히 망가지고, 며칠 뒤
"가끔 UART 데이터가 이상해요"라는 버그 리포트로 돌아온다.

### 해법 A — 크리티컬 섹션

```c
bool rb1_push_safe(rb1_t *q, int v) {
    uint32_t pri = __get_PRIMASK();
    __disable_irq();               /* 인터럽트 차단 */
    bool ok = rb1_push(q, v);
    __set_PRIMASK(pri);            /* 원래 상태 복구 (그냥 enable 하면 안 된다!) */
    return ok;
}
```

- 대가: **인터럽트 지연(latency) 증가.** 하드 리얼타임 시스템에서 최악 지연이 곧 사양이다.
- 함정: `__enable_irq()` 로 복구하면 **원래 꺼져 있던 경우까지 켜버린다.** 반드시 저장/복구.
- RTOS 라면 `taskENTER_CRITICAL()` / `portENTER_CRITICAL_FROM_ISR()` 로 컨텍스트별 API 를 써야 한다.

### 해법 B — count 를 아예 없앤다 → **레벨 2**

`used = head - tail` 로 계산하면 `count` 라는 공유 쓰기 변수 자체가 사라진다.
그러면 크리티컬 섹션 없이도 ISR ↔ 메인이 안전해진다. **이것이 레벨 2로 가는 진짜 이유다.**

> 면접 포인트: *"count 필드는 단일 스레드에서는 가장 읽기 쉬운 구현이지만, ISR 과 메인이
> 공유하는 순간 read-modify-write 경합이 생깁니다. lock-free 로 가려면 생산자와 소비자가
> **서로 다른 변수만 쓰도록** 구조를 바꿔야 하고, 그게 free-running 인덱스입니다."*

---

## 3. `tail` 은 사실 없어도 된다

`head` 와 `count` 가 있으면 `tail` 은 유도된다.

```c
unsigned rb1_tail_derived(const rb1_t *q) {
    return (q->head - q->count + RB1_CAP) % RB1_CAP;
}
```

필드 하나를 아낄 수 있지만 **읽을 때마다 나눗셈**이 붙는다. 보통은 그냥 `tail` 을 둔다.
이 계산이 의미 있는 순간은 **[레벨 5a 덮어쓰기 모드](06_level5_overwrite_zerocopy.html)** 다 —
거기서는 `tail` 이 생산자에 의해 밀려나므로 "항상 head 에서 count 만큼 뒤"라는 불변식이 오히려 자연스럽다.

---

## 4. 벌크 연산 — `push_n` / `pop_n`

바이트 하나씩 함수 호출하는 건 비싸다. UART DMA 로 64바이트가 한꺼번에 들어오면
64번 호출이 아니라 **한 번의 `memcpy` 두 조각**이어야 한다.

```c
unsigned rb1_push_n(rb1_t *q, const int *src, unsigned n) {
    unsigned space = RB1_CAP - q->count;
    if (n > space) n = space;              /* 부분 성공: 들어가는 만큼만 */

    unsigned first = RB1_CAP - q->head;    /* head 에서 배열 끝까지 */
    if (first > n) first = n;

    memcpy(&q->buf[q->head], src, first * sizeof(int));          /* 1조각 */
    memcpy(&q->buf[0], src + first, (n - first) * sizeof(int));  /* 2조각 (랩) */

    q->head = (q->head + n) % RB1_CAP;
    q->count += n;
    return n;
}
```

### 랩 구간을 두 조각으로 나누는 그림

```
  RB1_CAP=8, head=6, n=5 를 넣는다

  index   0    1    2    3    4    5    6    7
        [  ][  ][  ][  ][  ][  ][ A][ B]      ← first = 8-6 = 2  (A,B)
        [ C][ D][ E][  ][  ][  ][ A][ B]      ← n-first = 3      (C,D,E)
          ▲                        ▲
       두 번째 memcpy            첫 번째 memcpy 시작
```

**이 "2조각 분할"은 링버퍼의 모든 벌크 API 에 똑같이 나온다.** 레벨 2의 `rb2_write`,
레벨 4의 `rb4_write_all`, 레벨 5b 의 `zc_peek_iov` — 전부 같은 계산이다. 여기서 확실히 익혀라.

> `n - first` 가 0 일 때 `memcpy(dst, src, 0)` 은 안전하다(표준이 보장). 다만 **포인터가
> 유효해야** 하므로 `src + first` 가 배열 끝을 한 칸 넘는 것까지는 합법, 그 이상은 UB.

### 부분 성공(partial) vs 전부 아니면 전무(all-or-nothing)

| 정책 | 언제 | 예 |
|---|---|---|
| **부분 성공** — 들어가는 만큼 넣고 개수 리턴 | 바이트 스트림. 잘려도 의미가 있다 | UART TX 버퍼 |
| **all-or-nothing** — 전부 못 넣으면 하나도 안 넣음 | **레코드/프레임**. 반쪽은 파서를 깨뜨린다 | 로그 레코드, CAN 프레임, 텔레메트리 패킷 |

레벨 4에서 `rb4_write_all` 로 다시 만난다. 면접에서 "로그 링버퍼를 설계해보라"는 질문에
**"레코드 단위라 all-or-nothing 으로 하고, 실패하면 drop 카운터를 올립니다"** 라고 답할 수 있어야 한다.

---

## 5. 레벨 0 vs 레벨 1

| | 레벨 0 (`%` + 한 칸 희생) | 레벨 1 (`count`) |
|---|---|---|
| 8칸 배열의 실제 용량 | 7 | **8** |
| 구조체 크기 | +2 unsigned | +3 unsigned |
| empty/full 판정 | 인덱스 비교 (`%` 1회) | `count` 비교 (**연산 0회**) ✅ |
| 코드 가독성 | 보통 | **가장 읽기 쉽다** ✅ |
| 단일 스레드 | ✅ | ✅ |
| ISR ↔ 메인 (무락) | ⚠️ 어렵다 | ❌ **불가능** (count 경합) |

> 흥미로운 역설: **레벨 1은 가장 읽기 쉽지만 동시성에서는 가장 나쁘다.**
> 그래서 교과서(자료구조 수업)는 레벨 1을 가르치고, 펌웨어 코드베이스는 레벨 2를 쓴다.

---

## 6. 언제 실제로 레벨 1을 쓰나

버리는 레벨이 아니다. 아래 경우엔 레벨 1이 **정답**이다.

- 생산자와 소비자가 **같은 컨텍스트**(둘 다 메인 루프 태스크)
- 이미 mutex/크리티컬 섹션으로 감싸는 큐 (RTOS 큐 내부)
- 크기가 2의 거듭제곱이 아니어야 할 때 (예: 정확히 **10개**짜리 명령 큐)
- 가독성이 성능보다 중요한 곳 (설정 저장, UI 이벤트 큐)

레벨 2는 `size` 가 2의 거듭제곱이어야 한다는 제약이 있다. `count` 방식은 **아무 크기나** 된다.

---

## 7. 체크

- [ ] 8칸 배열에 8개가 전부 들어가는 걸 테스트로 확인했다
- [ ] `count++` 가 기계어 3단계라는 것과, 그 사이 인터럽트가 들어오면 무슨 일이 생기는지 설명할 수 있다
- [ ] 크리티컬 섹션에서 `__enable_irq()` 대신 PRIMASK 저장/복구를 해야 하는 이유를 안다
- [ ] `push_n` 의 랩 구간 2조각 분할을 그림으로 그릴 수 있다
- [ ] 부분 성공과 all-or-nothing 을 각각 언제 쓰는지 예를 들어 말할 수 있다
- [ ] `tail` 을 `head - count` 로 유도할 수 있다는 걸 테스트로 확인했다
- [ ] 레벨 1이 레벨 2보다 나은 상황을 하나 댈 수 있다

다음: **[레벨 2 — 마스킹과 free-running 인덱스](03_level2_mask.html)**
