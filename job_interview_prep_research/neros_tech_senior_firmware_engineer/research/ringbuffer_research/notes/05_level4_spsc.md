# ⚡ 레벨 4 — SPSC lock-free (인터럽트 ↔ 메인 루프)

> **면접 출제 확률 1위.** 지금까지는 "혼자 쓰는 링버퍼"였다. 이제 **ISR 이 넣고 메인 루프가 빼는**
> 진짜 상황으로 간다. 여기서 필요한 건 새로운 자료구조가 아니라 — 레벨 2 구조 그대로다 —
> **"왜 이게 락 없이 안전한가"를 증명하는 논리**와 **메모리 순서(memory ordering)** 다.
>
> 연습: `make prob N=L4_spsc`

---

## 1. 왜 ISR 에서 mutex 를 못 잡는가 ⭐

먼저 "그냥 락 걸면 되지 않나?"를 죽여야 한다. **4가지 이유**를 댈 수 있어야 한다.

1. **ISR 은 블록될 수 없다.** mutex 가 잠겨 있으면 `pend` 해야 하는데, ISR 은 스케줄러가
   재울 수 있는 실행 컨텍스트가 아니다. 재우는 순간 시스템이 정지한다.
2. **우선순위 역전 / 데드락.** 메인 태스크가 락을 쥔 채 ISR 이 들어오면, ISR 은 영원히 기다리고
   메인은 ISR 이 끝나야 실행되므로 **절대 락을 놓지 못한다.** 즉사 데드락.
3. **RTOS API 제약.** FreeRTOS 의 `xQueueSend` 는 ISR 에서 호출 금지다. `xQueueSendFromISR`
   같은 별도 변형을 써야 하고, 이건 내부적으로 **크리티컬 섹션**이지 mutex 가 아니다.
4. **지연(latency).** 락을 흉내내려고 `__disable_irq()` 를 쓰면 그 구간 동안 **모든** 인터럽트가
   막힌다. 최악 인터럽트 지연이 시스템 사양인 하드 리얼타임에서는 이게 곧 비용이다.

> 결론: ISR ↔ 메인에서 무락(lock-free)은 **선택이 아니라 필수**에 가깝다.

### 덤: ISR 에서 `printf` 가 왜 안 되는가 (같이 나온다)

1. **시간** — `snprintf` 는 수천~수만 사이클. ISR 예산은 보통 수 µs.
2. **스택** — 내부 버퍼·부동소수 변환으로 수백 바이트. ISR 스택 오버플로.
3. **재진입성** — `stdout` 은 전역 버퍼 + 락. newlib 의 `_impure_ptr` 는 ISR 안전이 아니다.
4. **블로킹 I/O** — 115200bps 에서 40바이트 = **3.5ms** 동안 인터럽트 컨텍스트 점유. 제어 루프 사망.

> 답: ISR 은 **고정 크기 바이너리 레코드를 링에 복사만** 하고 끝낸다(수십 사이클).
> 포맷팅은 소비자(저우선순위 태스크)나 **호스트 PC** 로 미룬다. → `practice/` 의 `01_ring_logging` 세트.

---

## 2. SPSC 가 무락으로 성립하는 조건 (증명의 뼈대)

| 조건 | 왜 필요한가 |
|---|---|
| 생산자 **정확히 1개**, 소비자 **정확히 1개** | 2명이 같은 인덱스를 store 하면 write-write 경합 → 즉시 붕괴 |
| `head` 는 **생산자만** store, `tail` 은 **소비자만** store | 서로 상대 인덱스는 **읽기만** → 이게 무락의 근거 전부 |
| 공유 `count` 필드 **금지** | 양쪽이 갱신 → 경합. 그래서 `used = head - tail` ([레벨 2](03_level2_mask.html)) |
| 인덱스 load/store 가 **원자적** (tearing 없음) | 32비트 MCU 의 정렬된 32비트 접근은 원자적. 8비트 AVR 의 16비트 인덱스는 **아니다** |
| **데이터 먼저, 인덱스 나중** (+ 배리어) | 순서가 뒤집히면 소비자가 쓰레기를 읽는다 |

`ISR ↔ main` 은 SPSC 의 교과서 사례다. **ISR 하나가 생산자, 메인 루프가 소비자.**

> ⚠️ 흔한 사고: 같은 링에 **두 개의 ISR**(UART1, UART2)이 쓴다. 그 순간 SPSC 가 아니라 MPSC 이고
> 위 논증이 전부 무효다. [07 노트](07_pitfalls_mpmc.html) 참조.

---

## 3. ⭐⭐ 왜 `volatile` 로는 부족한가

임베디드 코드에서 제일 흔한 오해다.

```c
volatile uint32_t head, tail;   /* 이러면 안전한가? */
```

`volatile` 이 보장하는 것과 보장하지 않는 것:

| | `volatile` | `_Atomic` / `stdatomic` |
|---|---|---|
| 컴파일러가 변수를 레지스터에 캐싱하는 것 방지 | ✅ | ✅ |
| 컴파일러가 접근을 **제거/병합**하는 것 방지 | ✅ | ✅ |
| **컴파일러의 재배치(reordering) 방지** | ⚠️ volatile 끼리만. **일반 변수와의 순서는 보장 안 함** | ✅ |
| **CPU 의 재배치 / store buffer 방지** | ❌ **전혀 안 함** | ✅ |
| 원자성(read-modify-write) | ❌ | ✅ |

핵심은 세 번째 줄이다.

```c
buf[head & mask] = data;   /* 일반 변수 접근 */
head = head + 1;           /* volatile 접근 */
```

컴파일러는 `volatile` 접근끼리의 순서는 지키지만, **일반 변수인 `buf[]` 쓰기와 `head` 쓰기 사이의
순서는 지킬 의무가 없다.** 게다가 CPU 는 store buffer 에서 순서를 바꿔 내보낼 수 있다.

### 그런데 왜 실무 코드는 `volatile` 로도 잘 돌까?

**단일 코어 Cortex-M** 에서는:
- 인터럽트 진입/복귀가 사실상 배리어처럼 동작한다 (파이프라인 flush, 예외 진입 시 메모리 순서 보장)
- Cortex-M0/M3/M4 는 out-of-order 실행이 없고 store buffer 도 사실상 관측되지 않는다

그래서 **운 좋게 동작한다.** 하지만:
- Cortex-M7 은 store buffer 와 write-back 캐시가 있다
- Cortex-A / 멀티코어 / Linux SMP 에서는 **반드시 깨진다**
- 컴파일러 최적화 레벨을 올리거나 컴파일러를 바꾸면 깨질 수 있다
- LTO 가 켜지면 더 깨진다

> **면접 답변**: *"volatile 은 컴파일러 최적화만 억제하고 CPU 메모리 배리어는 보장하지 않습니다.
> 단일 코어 Cortex-M 에서는 인터럽트 진입이 사실상 배리어라 우연히 동작하지만, M7 의 store buffer나
> Cortex-A 멀티코어에서는 깨집니다. C11 `stdatomic` 의 acquire/release 를 쓰면 컴파일러와 CPU
> 양쪽에 순서를 지시할 수 있고, 배리어가 필요 없는 아키텍처에서는 컴파일러가 아무 명령도 안 냅니다 —
> 즉 공짜입니다."*

### `stdatomic` 이 없던 시절의 관용구

```c
volatile uint32_t head, tail;

buf[head & mask] = data;
__DMB();              /* Data Memory Barrier — CMSIS 인트린식 */
head = head + 1;
```

정확하고, 지금도 많은 HAL 코드가 이 형태다. 다만 **어느 배리어를 어디에** 넣을지를 사람이 판단해야 해서
실수하기 쉽다. `stdatomic` 은 그 판단을 타입에 박아 넣는다.

---

## 4. 메모리 순서 — 정확히 이 4줄만 외우면 된다

```c
typedef struct {
    uint8_t              *buf;
    uint32_t              size, mask;
    atomic_uint_fast32_t  head;      /* 생산자만 store */
    atomic_uint_fast32_t  tail;      /* 소비자만 store */
    uint32_t              dropped;   /* 생산자만 갱신 → 원자화 불필요 */
} rb4_t;
```

### 생산자 (ISR)

```c
bool rb4_push(rb4_t *q, uint8_t v) {
    uint32_t h = atomic_load_explicit(&q->head, memory_order_relaxed);
    /*  ↑ 내가 쓴 값을 내가 읽는다. 다른 누구와도 동기화할 게 없다 → relaxed */

    uint32_t t = atomic_load_explicit(&q->tail, memory_order_acquire);
    /*  ↑ 소비자가 비워준 공간을 본다. 소비자의 "다 읽었다"는 release store 와 짝을 이뤄
         소비자가 그 칸을 다 읽은 뒤에야 내가 덮어쓰도록 보장한다 → acquire */

    if (h - t == q->size) { q->dropped++; return false; }

    q->buf[h & q->mask] = v;                      /* ★ 데이터 먼저 */

    atomic_store_explicit(&q->head, h + 1, memory_order_release);
    /*  ↑ 인덱스 나중. release 는 "이 store 이전의 모든 쓰기가 이 store 보다 먼저 보인다"를
         보장한다. 소비자가 acquire 로 head 를 읽으면 buf 내용도 반드시 보인다 → release */
    return true;
}
```

### 소비자 (메인 루프)

```c
bool rb4_pop(rb4_t *q, uint8_t *out) {
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_relaxed);   /* 내 변수 */
    uint32_t h = atomic_load_explicit(&q->head, memory_order_acquire);   /* 생산자와 짝 */

    if (h == t) return false;

    *out = q->buf[t & q->mask];                                          /* 데이터 읽고 */

    atomic_store_explicit(&q->tail, t + 1, memory_order_release);        /* 나중에 공개 */
    return true;
}
```

### 외우는 법 — **"내 것은 relaxed, 남의 것은 acquire, 내 공개는 release"**

```
      생산자                                    소비자
  ┌──────────────────┐                    ┌──────────────────┐
  │ head: relaxed load│ (내 것)            │ tail: relaxed load│ (내 것)
  │ tail: acquire load│◄───── 짝 ────────►│ tail: release store│
  │ buf[] = data      │                    │ data = buf[]      │
  │ head: release store│◄──── 짝 ─────────►│ head: acquire load│
  └──────────────────┘                    └──────────────────┘
```

**release-acquire 쌍이 두 개**다. 하나는 "데이터가 다 써졌다"를 전달하고, 다른 하나는
"그 칸을 다 읽었으니 덮어써도 된다"를 전달한다.

> Cortex-M 처럼 단일 코어 in-order 코어에서 acquire/release 는 **컴파일러 배리어로만 낮춰지고
> CPU 명령은 하나도 안 나간다.** 즉 `volatile` 대비 런타임 비용이 0 인데 정확성은 훨씬 높다.
> **쓰지 않을 이유가 없다.**

---

## 5. ⭐ 순서를 뒤집으면 무슨 일이 생기나 (사고 실험)

일부러 틀리게 짠 버전:

```c
/* ❌ 절대 이렇게 쓰지 말 것 */
void rb4_push_broken(rb4_t *q, uint8_t v) {
    uint32_t h = q->head;
    q->head = h + 1;                 /* 인덱스 먼저 올리고 */
    q->buf[h & q->mask] = v;         /* 데이터 나중에 */
}
```

**단일 스레드 테스트는 전부 통과한다.** 그래서 무섭다.

```
 시각   ISR(생산자)                   메인(소비자)               결과
 ────   ─────────────                 ──────────────             ────
  t0    head = 5 → 6
  t1                                  head(6) != tail(5) → 있다!
  t2                                  *out = buf[5]              💥 쓰레기 값
  t3    buf[5] = 0xAB                                            (너무 늦었다)
```

컴파일러/CPU 의 재배치가 없어도 **인터럽트 타이밍만으로** 터진다. 재현율은 수십만 번에 한 번이라
현장에서 "가끔 이상한 값이 들어와요"로 나타난다. **디버깅이 거의 불가능한 종류의 버그다.**

> 교훈: 동시성 버그는 **테스트로 못 잡는다. 구조로 막아야 한다.**
> 그래서 "데이터 먼저, 인덱스 나중"을 규칙으로 외우고, `release` store 로 **컴파일러에게도 강제**한다.

---

## 6. ⭐ `used()` 가 "보수적으로 안전하게 틀린다"

멀티 컨텍스트에서 `rb4_used()` 의 리턴값은 **읽는 순간 이미 낡았다.** 그런데도 안전하다.

| 누가 부르나 | 실제 값과의 관계 | 왜 안전한가 |
|---|---|---|
| **생산자**가 `free()` 를 본다 | 실제 free ≤ 본 값... 이 아니라 **실제 free ≥ 본 값** | 소비자가 그 사이 더 빼서 공간이 **늘어날 뿐**. 생산자는 "공간이 부족하다"고 판단해 **덜 넣는다** → 안전 |
| **소비자**가 `used()` 를 본다 | 실제 used ≥ 본 값 | 생산자가 그 사이 더 넣어 데이터가 **늘어날 뿐**. 소비자는 "데이터가 없다"고 판단해 **덜 뺀다** → 안전 |

핵심: **각자의 오차가 항상 안전한 방향으로 난다.** 생산자는 절대 오버플로하지 않고,
소비자는 절대 아직 안 써진 데이터를 읽지 않는다. 최악의 결과는 "한 번 더 폴링하면 되는 기회 손실"뿐.

> 이 성질을 말로 설명할 수 있으면 면접에서 크게 먹힌다. "값이 낡았는데 왜 괜찮죠?"에 대한 답이다.

---

## 7. all-or-nothing 벌크 쓰기 + drop 카운터

```c
bool rb4_write_all(rb4_t *q, const uint8_t *src, uint32_t n) {
    uint32_t h = atomic_load_explicit(&q->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_acquire);

    if (q->size - (h - t) < n) {       /* 전부 못 넣는다 */
        q->dropped++;                  /* ★ 조용히 죽지 말고 숫자로 남긴다 */
        return false;                  /* ★ 한 바이트도 안 넣는다 */
    }
    /* ... 2조각 memcpy ... */
    atomic_store_explicit(&q->head, h + n, memory_order_release);
    return true;
}
```

**왜 부분 쓰기를 하면 안 되는가**: 로그/텔레메트리는 레코드 단위다. 16바이트 레코드 중 9바이트만
들어가면 소비자 파서가 **그 지점부터 영구히 어긋난다.** 링버퍼 하나가 망가지는 게 아니라
**로그 전체가 못 쓰게 된다.**

**`dropped` 를 생산자만 건드리는 이유**: 원자화하면 ISR 안에서 비용이 붙는다. 생산자 전용 변수로
두면 경합이 없다. 소비자는 `dropped` 를 **읽기만** 하고 (부정확해도 진단용이라 상관없다), 진단
메시지에 찍는다: `"log: 1423 records dropped"`.

> 규칙: **유실은 일어난다. 중요한 건 유실을 숫자로 말할 수 있느냐다.**

---

## 8. 불변식(conservation law) — 테스트하는 법

동시성 코드는 "값이 맞다"로 테스트할 수 없다. **불변식**으로 테스트한다.

```
  생산한 총 바이트 == 소비한 총 바이트 + 링에 남은 바이트 + 드롭된 바이트
```

`L4_spsc.c` 의 Q5 가 이걸 검증한다. ISR 시뮬레이션과 드레인을 200회 뒤섞어 돌리고
매 스텝마다 이 등식을 확인한다. **값 하나하나가 아니라 등식이 깨지지 않는지를 본다.**

실제 장비에서도 똑같이 한다 — 부팅 후 1시간 돌리고 `produced == consumed + used + dropped` 를
찍어 본다. 안 맞으면 어딘가에서 데이터가 증발하고 있다는 뜻이다.

---

## 9. false sharing (Linux/멀티코어 타깃에서만)

```c
typedef struct {
    alignas(64) atomic_uint_fast32_t head;   /* 생산자 캐시라인 */
    alignas(64) atomic_uint_fast32_t tail;   /* 소비자 캐시라인 */
    uint8_t *buf; uint32_t size, mask;
} rb4_aligned_t;
```

`head` 와 `tail` 이 **같은 64바이트 캐시라인**에 있으면, 생산자가 `head` 를 쓸 때마다
소비자 코어의 캐시라인이 무효화된다. 값은 정확하지만 **성능이 몇 배 떨어진다.**

- **MCU 에는 캐시가 없어 무의미하다.** RAM 만 64바이트씩 낭비한다.
- **Cortex-A / x86 Linux 타깃에서는 필수적**인 최적화다.
- Neros 처럼 **MCU 와 Linux 양쪽에 같은 코드를 올리는** 플랫폼이라면
  `#if` 로 정렬을 조건부로 거는 게 실전적이다.

---

## 10. 체크

- [ ] ISR 에서 mutex 를 못 잡는 이유 4가지를 막힘없이 말할 수 있다
- [ ] ISR 에서 `printf` 가 안 되는 이유 4가지를 말할 수 있다
- [ ] SPSC 무락 성립 조건 5가지를 말할 수 있다
- [ ] `volatile` 이 보장하는 것과 안 하는 것을 표로 그릴 수 있다
- [ ] "내 것은 relaxed, 남의 것은 acquire, 내 공개는 release" 를 코드로 안 보고 쓸 수 있다
- [ ] release-acquire 쌍이 **두 개** 있고 각각 무엇을 전달하는지 설명할 수 있다
- [ ] 데이터/인덱스 순서를 뒤집으면 왜 단일 스레드 테스트로는 안 잡히는지 설명할 수 있다
- [ ] `used()` 의 오차가 왜 항상 안전한 방향인지 생산자/소비자 각각 설명할 수 있다
- [ ] all-or-nothing 이 필요한 이유(파서 붕괴)와 drop 카운터가 필요한 이유를 말할 수 있다
- [ ] 보존 법칙 불변식을 테스트로 쓸 수 있다
- [ ] false sharing 이 MCU 에서는 왜 무의미한지 안다
- [ ] 노트를 덮고 `rb4_push` / `rb4_pop` 을 메모리 오더까지 정확히 다시 짰다

다음: **[레벨 5 — 덮어쓰기 모드와 zero-copy](06_level5_overwrite_zerocopy.html)**
