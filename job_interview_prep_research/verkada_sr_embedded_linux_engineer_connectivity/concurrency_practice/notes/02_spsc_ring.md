# 02. Lock-free SPSC ring buffer — 베어메탈 링버퍼를 C11 atomics 로 다시 쓰기

> **이 노트를 다 읽으면**
> - 락 없이 생산자 1 · 소비자 1 이 안전하게 주고받는 링버퍼를 직접 짤 수 있다
> - release store 와 acquire load 가 무엇을 보장하는지 "데이터 먼저, 공개는 나중"으로 설명할 수 있다
> - `volatile` 로는 왜 안 되는지, 베어메탈에서는 왜 그래도 돌았는지 말할 수 있다
>
> **선행**: `00_start_here` 의 §5 (원자 연산과 memory order), §6 (실시간 데이터 전달 패턴)
> **연습**: `make run N=02` (내 구현) · `make sol N=02` (모범답안) · `make tsan N=02`

---

## 0. 한 문장으로

**생산자 하나와 소비자 하나가 락 없이 `head`/`tail` 인덱스만으로 동기화하는 고정 크기 링버퍼**다. 게이트웨이에서 LTE 모뎀의 UART RX 인터럽트가 바이트를 넣고 AT 응답 파서 태스크가 꺼내는 경로가 바로 이것이고, **ISR 안에서는 mutex 를 쓸 수 없기 때문에** 락이 아닌 atomic 으로 풀어야 한다.

---

## 1. 왜 필요한가 — 이미 만들어 본 것

베어메탈에서 UART 링버퍼를 짜 본 적이 있다면 구조는 이미 안다. 달라지는 건 **동기화의 근거**뿐이다.

```
   UART RX ISR (생산자)                          파서 task (소비자)
   ─────────────────────                         ─────────────────
        buf[tail] = b                                 b = buf[head]
        tail++                                        head++
              │                                             │
              ▼                                             ▼
   ┌────┬────┬────┬────┬────┬────┬────┬────┬ ... ┬────┐
   │ 41 │ 54 │ 0D │ 0A │    │    │    │    │     │    │   RING_CAP = 64
   └────┴────┴────┴────┴────┴────┴────┴────┴ ... ┴────┘
     ▲                   ▲
    head                tail          생산자는 tail 만 쓰고, 소비자는 head 만 쓴다
```

**왜 락을 안 쓰나?** mutex 는 경쟁이 생기면 호출자를 **재운다**. ISR 은 잠들 수 없다(인터럽트 컨텍스트는 스케줄 대상이 아니다). 사용자 공간이라도 오디오 콜백이나 실시간 스레드에서 블로킹은 곧 글리치다.

**왜 락이 없어도 되나?** 생산자와 소비자가 **서로 다른 변수만 쓰기** 때문이다. 생산자는 `tail` 만 store 하고 `head` 는 load 만 한다. 소비자는 그 반대다. 같은 변수에 두 스레드가 동시에 쓰는 일이 원천적으로 없으니 상호배제가 필요 없다. 남는 문제는 하나뿐이다 — **"서로가 상대의 인덱스를 언제, 어떤 순서로 보게 되는가."** 그게 memory order 이야기다.

이 구조는 **SPSC(Single Producer Single Consumer)** 에서만 성립한다. 생산자가 둘이 되면 `tail` 에 두 스레드가 쓰게 되어 전부 무너진다. 그때는 CAS 루프나 락으로 가야 한다.

---

## 2. 알아야 할 개념

| 용어 | 한 줄 정의 | 왜 존재하나 / 내가 아는 것과의 대응 |
|---|---|---|
| SPSC | 생산자 1, 소비자 1 전용 | 이 가정 덕분에 CAS 없이 store/load 만으로 된다 |
| lock-free | 락 없이 atomic 으로만 진행 | ISR/실시간 경로에서 블로킹이 금지이기 때문 |
| `_Atomic` | C11 원자 타입. 원자성 + 메모리 순서 | 베어메탈에서 쓰던 `volatile` 의 **제대로 된** 대체품 |
| `memory_order_relaxed` | 원자성만, 순서 보장 없음 | 내가 유일한 writer 인 값을 내가 읽을 때 |
| `memory_order_acquire` | 이 load 뒤의 접근이 앞으로 못 넘어옴 | 상대가 publish 한 인덱스를 읽을 때 |
| `memory_order_release` | 이 store 앞의 접근이 뒤로 못 넘어감 | 데이터를 다 쓰고 인덱스를 공개할 때 |
| power-of-two mask | `% cap` 대신 `& (cap-1)` | 나눗셈 제거. 베어메탈에서 쓰던 그 트릭 |
| 한 칸 비우기 | full/empty 구분을 위해 한 슬롯 희생 | `head == tail` 하나로 두 상태를 표현할 수 없어서 |

```c
typedef struct {
    uint8_t              buf[RING_CAP];
    _Atomic unsigned     head;             /* 소비자만 store */
    _Atomic unsigned     tail;             /* 생산자만 store */
} Ring;
```

주석 두 줄이 **불변식**이자 이 설계의 전부다. "누가 무엇을 쓰는가"를 못 박아 두면 나머지는 따라온다. `buf` 가 atomic 이 아닌 것도 의도적이다 — 인덱스의 release/acquire 가 데이터까지 같이 보호해 주므로 데이터 자체를 atomic 으로 만들 필요가 없다(§4.3).

---

## 3. 문제 읽기

| 요구 | 설계에 주는 제약 |
|---|---|
| ISR 에서 호출 가능 | 블로킹 금지. mutex, malloc, printf 전부 금지 |
| 생산자 1 / 소비자 1 | 각자 자기 인덱스만 store → CAS 불필요 |
| 가득 차면 실패 반환 | `ring_push` 는 `bool` 반환. 호출자가 drop 정책 결정 |
| 비면 실패 반환 | `ring_pop` 도 `bool`. 폴링하거나 이벤트로 깨운다 |
| 데이터 순서·내용 보존 | release/acquire 짝이 반드시 필요 |
| 나눗셈 없이 | `RING_CAP` 을 2의 거듭제곱으로 고정 |

```c
#define RING_CAP 64u                       /* power of two */
```

2의 거듭제곱이면 `x % 64` 를 `x & 63` 으로 바꿀 수 있다. 하드웨어 나눗셈이 없거나 느린 코어에서 ISR 안의 나눗셈은 그 자체로 비용이고, 뒤에서 볼 `(t - h) & (RING_CAP - 1u)` 같은 **wraparound 계산이 공짜로 맞아떨어진다.**

---

## 4. 단계별로 만들기

### 4.1 초기화 — `atomic_init`

```c
void ring_init(Ring *r)
{
    atomic_init(&r->head, 0u);
    atomic_init(&r->tail, 0u);
}
```

`_Atomic` 변수는 `= 0` 대신 `atomic_init` 으로 초기화하는 게 정석이다. `atomic_init` 은 **동기화를 하지 않는 초기화**라 다른 스레드가 이미 접근 중일 때 쓰면 안 된다. 즉 "스레드를 만들기 전에 한 번" 부르는 함수다.

### 4.2 push — 내 인덱스는 relaxed, 상대 인덱스는 acquire

```c
bool ring_push(Ring *r, uint8_t b)
{
    unsigned t = atomic_load_explicit(&r->tail, memory_order_relaxed); /* 내가 쓴 값 */
    unsigned next = (t + 1u) & (RING_CAP - 1u);
    /* 소비자가 얼마나 비웠는지 본다 → acquire */
    if (next == atomic_load_explicit(&r->head, memory_order_acquire))
        return false;                      /* full (한 칸은 항상 비워둔다) */
```

- `tail` 을 `relaxed` 로 읽는 이유: **`tail` 을 쓰는 스레드는 나뿐이다.** 내가 쓴 값을 내가 읽는 건 단일 스레드 이야기라 순서를 맞출 상대가 없다. acquire 를 붙여도 틀리진 않지만 공짜 배리어는 없다.
- `head` 를 `acquire` 로 읽는 이유: **소비자가 publish 한 값**이다. 소비자가 슬롯을 다 읽고 나서 `head` 를 올렸다는 사실을, 내가 그 슬롯을 덮어쓰기 **전에** 확실히 봐야 한다.
- `next == head` 가 **full 판정**이다. 한 칸을 항상 비워 둔다는 뜻이고, 실제 저장 가능한 최대 개수는 63 이다.

**왜 한 칸을 비우나?** `head == tail` 이 "비었다"와 "가득 찼다" **둘 다**를 의미하면 구분할 방법이 없다. 선택지는 셋이다 — 별도 `count` 를 두거나(두 스레드가 같은 변수를 쓰게 되어 SPSC 의 장점이 사라진다), 랩 카운트를 쓰거나, 한 칸을 희생하거나. 64바이트 버퍼에서 1바이트는 싸다.

### 4.3 push — 데이터 먼저, 공개는 나중에

```c
    r->buf[t] = b;                         /* 데이터 먼저 */
    /* release: 위의 데이터 write가 tail publish 이전에 보이도록 보장 */
    atomic_store_explicit(&r->tail, next, memory_order_release);
    return true;
}
```

이 두 줄이 이 문제의 **심장**이다. 소스 순서는 "데이터 → 인덱스"지만, 컴파일러와 CPU 는 서로 다른 주소를 건드리는 두 store 를 **단일 스레드 결과가 같으면** 얼마든지 뒤바꿀 수 있다.

```
 소비자 코어에 실제로 보이는 순서 (release 가 없을 때)
 ────────────────────────────────────────────────────────
   tail = next        ← 먼저 보임: "새 바이트가 있다!"
   buf[t] = b         ← 아직 안 보임

 소비자:  h != tail → *out = r->buf[h];     ← 아직 안 써진 옛 값/쓰레기를 읽는다
```

증상은 "20만 바이트에 한 번 값이 이상하다"이다. `memory_order_release` 를 붙이면 **이 store 이전의 모든 메모리 접근이 이 store 보다 뒤로 넘어갈 수 없다.** 그래서 소비자가 새 `tail` 을 본 순간 `buf[t] = b` 는 이미 보이는 것이 보장된다.

### 4.4 pop — 거울 대칭

```c
bool ring_pop(Ring *r, uint8_t *out)
{
    unsigned h = atomic_load_explicit(&r->head, memory_order_relaxed);
    /* acquire: tail을 읽은 뒤 buf를 읽어야 생산자의 데이터가 보인다 */
    if (h == atomic_load_explicit(&r->tail, memory_order_acquire))
        return false;                      /* empty */

    *out = r->buf[h];
    atomic_store_explicit(&r->head, (h + 1u) & (RING_CAP - 1u), memory_order_release);
    return true;
}
```

같은 규칙의 반대편이다. `head` 는 내가 쓰는 값이라 `relaxed`, `tail` 은 상대가 publish 한 값이라 `acquire`. `*out = r->buf[h];` 가 **acquire load 다음**에 있어야 컴파일러가 버퍼 읽기를 tail 검사보다 먼저 당기지 못한다.

마지막 `head` store 에 `release` 를 쓰는 이유는 덜 분명한데, **생산자가 이 슬롯을 덮어쓰는 것을 막기 위해서**다. "나는 `buf[h]` 를 다 읽었다"를 publish 하는 것이고 생산자는 acquire 로 그것을 읽는다. 이 release 가 빠지면 생산자가 아직 읽히지 않은 슬롯에 새 바이트를 써 버릴 수 있다. 정리하면 **release/acquire 짝이 이 코드에 두 개** 있다.

| 짝 | 보호하는 것 |
|---|---|
| 생산자 `store(tail, release)` ↔ 소비자 `load(tail, acquire)` | 데이터가 쓰인 뒤에 읽힌다 |
| 소비자 `store(head, release)` ↔ 생산자 `load(head, acquire)` | 데이터가 읽힌 뒤에 덮어써진다 |

### 4.5 count — 스냅샷일 뿐이라는 점

```c
unsigned ring_count(const Ring *r)
{
    unsigned t = atomic_load_explicit(&r->tail, memory_order_acquire);
    unsigned h = atomic_load_explicit(&r->head, memory_order_acquire);
    return (t - h) & (RING_CAP - 1u);
}
```

`t - h` 는 `unsigned` 뺄셈이라 `t` 가 랩해서 `h` 보다 작아져도 결과가 맞다. `t = 2`, `h = 62` 면 `2 - 62` 는 unsigned 에서 큰 값이 되지만 `& 63` 을 하면 4 가 나온다 — 실제로 62, 63, 0, 1 네 칸이 차 있다. **mask 덕분에 wraparound 산술이 공짜로 맞는다.**

주의할 점은 이 값이 **즉시 낡는다**는 것이다. 두 load 사이에도 상대가 인덱스를 움직일 수 있다. 그래서 `ring_count` 는 텔레메트리/디버깅용이지 제어 판단용이 아니다. "공간이 있으니 push 하겠다"처럼 **검사 후 행동**으로 쓰면 TOCTOU 버그다. 그래서 `ring_push` 는 `ring_count` 를 쓰지 않고 자기 안에서 직접 full 을 판정한다.

---

## 5. 전체 흐름 따라가기

빈 링에서 한 바이트가 건너가는 과정이다.

```
 시간 ↓  생산자 (ISR)                        소비자 (task)               head  tail
  t0                                        load(head,relaxed)=0          0     0
  t1    load(tail,relaxed)=0                load(tail,acquire)=0          0     0
  t2    next = 1                            0 == 0 → empty, return false  0     0
  t3    load(head,acquire)=0                                              0     0
  t4    1 != 0 → 공간 있음                                                 0     0
  t5    buf[0] = 0x41           ┐                                         0     0
  t6    store(tail,1,RELEASE)   ●─── synchronizes-with ──┐                0     1
  t7                                        load(head,relaxed)=0          0     1
  t8                                  ○ ◀───┘ load(tail,acquire)=1        0     1
  t9                                        0 != 1 → 데이터 있음           0     1
  t10                                       *out = buf[0]  → 0x41 보장    0     1
  t11                                       store(head,1,RELEASE)         1     1
```

t6 의 release 와 t8 의 acquire 가 짝을 이루는 순간, **t5 의 `buf[0] = 0x41` 이 t10 에서 반드시 보인다**는 보장이 생긴다(happens-before). 이게 "데이터 먼저, 공개는 나중" 이다.

가득 찬 경우는 이렇다. `tail = 63`, `head = 0` 상태에서 `next = (63 + 1) & 63 = 0` 이고 `next == head` 이므로 full 이다. `buf[63]` 은 비어 있지만 쓰지 않는다 — 63개만 저장하고 한 칸은 영원히 비워 둔다. 이게 full 과 empty 를 구분하는 값이다.

---

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
|---|---|---|---|
| `_Atomic` 대신 `volatile` | 200k 바이트에 한 번 값이 깨짐. 멀티코어에서만 재현 | `volatile` 은 재정렬을 막지 않는다 | `_Atomic` + release/acquire |
| 전부 `relaxed` | 같은 증상. 단일 코어에서는 잘 돌아감 | 순서 보장이 없어 인덱스가 데이터보다 먼저 보임 | publish 는 release, 상대 인덱스는 acquire |
| 데이터 write 를 store 뒤로 | 거의 항상 깨짐 | 공개 후에 쓰면 소비자가 옛 값을 본다 | `buf[t] = b;` → `store(tail, release)` 순서 |
| `head` store 의 release 생략 | 아주 드물게 읽는 중에 값이 바뀜 | 생산자가 아직 읽히지 않은 슬롯을 덮어씀 | `store(head, ..., release)` |
| 한 칸 비우기를 안 함 | full 인데 empty 로 판단(또는 반대) → 전부 유실 | `head == tail` 이 두 상태를 동시에 의미 | `next == head` 를 full 판정으로 |
| `RING_CAP` 이 2의 거듭제곱이 아님 | 인덱스가 범위를 벗어나거나 count 가 틀림 | `& (CAP-1)` 마스크가 성립하지 않음 | 2의 거듭제곱 유지, 아니면 `%` 사용 |
| 생산자를 2개로 늘림 | 바이트 유실/중복, 재현 어려움 | 두 스레드가 `tail` 에 store → SPSC 가정 붕괴 | 락이나 CAS 기반 MPSC 로 교체 |

---

## 7. 직접 확인하기

```sh
make sol N=02
```

```
tx_sum=25493856 rx_sum=25493856 retries=66356 final_count=0
02 PASS
```

`tx_sum` 과 `rx_sum` 이 같다 = 보낸 바이트의 합과 받은 합이 일치한다. `final_count=0` = 끝났을 때 링이 비어 있다. `retries` 는 링이 가득 차서 재시도한 횟수라 **실행할 때마다 값이 다르다**(수천~수만). 두 스레드의 상대 속도에 따라 달라지는 것이라 정상이다. 합만 맞고 순서가 틀리는 버그를 잡으려고 매 바이트를 기대값과 비교하는 `assert(b == (uint8_t)(expect & 0xFF));` 도 들어 있다.

self test 의 생산자는 실제 ISR 과 다르게 동작한다는 점을 꼭 알아 두자.

```c
while (!ring_push(&g_ring, b)) {  /* 실제 ISR은 drop + 카운트만 한다 */
    g_dropped++;                  /* 여기선 버리지 않고 재시도 */
}
```

테스트는 합을 검증해야 하므로 재시도하지만, **진짜 UART ISR 은 여기서 재시도하면 안 된다.** ISR 안에서 도는 순간 다음 인터럽트를 놓치고 하드웨어 오버런이 난다. 실제 구현은 `if (!ring_push(...)) drop_count++;` 로 버리고 즉시 나가야 하며, `drop_count` 는 텔레메트리로 올려서 "왜 AT 응답이 깨지는가"를 나중에 추적할 수 있게 한다. 면접에서 이 구분을 말하면 크게 먹힌다.

```sh
make tsan N=02
```

모범답안은 TSan 이 깨끗하다. **TSan 은 atomic 을 이해하기 때문에** release/acquire 로 제대로 묶인 `buf` 접근에는 경고를 내지 않는다. 반대로 `_Atomic` 을 `volatile` 로 바꾸면 `WARNING: ThreadSanitizer: data race` 가 `ring_push` 의 `buf` write 와 `ring_pop` 의 `buf` read 를 지목하며 즉시 뜬다. "그 둘 사이에 happens-before 가 없다"는 뜻이고, `volatile` 이 동기화 도구가 아님을 **도구가 직접 증명해 주는** 실험이라 한 번 해 보는 걸 권한다.

---

## 8. 면접에서 말하기

- "SPSC 라서 생산자는 `tail` 만, 소비자는 `head` 만 store 합니다. 같은 변수에 동시에 쓰는 일이 없으니 상호배제가 필요 없고, 남는 문제는 가시성 순서뿐이라 atomic 으로 풉니다."
- "생산자는 데이터를 먼저 쓰고 `tail` 을 release store 로 공개하고, 소비자는 `tail` 을 acquire load 로 읽습니다. 이 짝이 있어야 소비자가 새 인덱스를 본 순간 데이터도 보입니다."
- "자기 인덱스는 relaxed 로 읽습니다. 그 변수의 writer 가 자기뿐이라 순서를 맞출 상대가 없습니다."
- "`volatile` 은 컴파일러 최적화만 막고 원자성도 메모리 순서도 주지 않습니다. 베어메탈 단일 코어에서 돌던 건 재정렬이 관찰되지 않았기 때문이지 `volatile` 이 맞아서가 아닙니다."
- "실제 ISR 은 full 일 때 재시도하지 않고 버리고 카운터를 올립니다. drop 은 반드시 세서 텔레메트리로 올려야 현장에서 링 크기가 부족한지 판단할 수 있습니다."

- "Because it's single-producer single-consumer, each side owns one index — the producer only stores to `tail`, the consumer only to `head`. No mutual exclusion is needed."
- "The producer writes the payload first, then publishes the index with a release store; the consumer does an acquire load on that index before touching the buffer. Data first, publish second."
- "The index each side owns is read with relaxed ordering, since that thread is its only writer."
- "`volatile` only stops the compiler from caching the access. It gives neither atomicity nor ordering, so it cannot replace acquire/release on a multicore SoC."
- "In a real ISR I'd drop on full and bump a counter rather than retry, and export that counter as telemetry."

---

## 9. 요약 & 체크리스트

SPSC 링버퍼는 **"각자 자기 인덱스만 쓴다"** 는 가정 하나로 락을 없앤 구조다. 남는 유일한 문제인 가시성 순서를 release/acquire 짝 **두 개**로 해결한다 — 생산자의 `tail` publish 와 소비자의 `head` publish. 용량은 2의 거듭제곱으로 잡아 나눗셈을 없애고, 한 칸을 비워 full 과 empty 를 구분한다. 이 문제는 베어메탈 UART 링버퍼 경험을 "C11 memory model 을 이해한다"는 신호로 바꿔 주는 다리다.

- [ ] 생산자/소비자가 각각 어떤 변수를 store 하고 어떤 변수를 load 하는지 말할 수 있다
- [ ] release/acquire 짝이 왜 두 개인지, 각각 무엇을 보호하는지 설명할 수 있다
- [ ] 자기 인덱스를 `relaxed` 로 읽어도 되는 이유를 말할 수 있다
- [ ] release 가 없을 때 깨지는 시나리오를 타임라인으로 그릴 수 있다
- [ ] 한 칸을 비워 두는 이유와 실제 용량이 63 인 것을 설명할 수 있다
- [ ] `(t - h) & (RING_CAP - 1u)` 가 wraparound 에서도 맞는 이유를 설명할 수 있다
- [ ] `volatile` 이 못 하는 세 가지를 댈 수 있다
- [ ] 생산자가 둘이 되면 왜 무너지는지, 그때 무엇으로 바꿀지 말할 수 있다
- [ ] `make sol N=02` 와 `make tsan N=02` 를 둘 다 통과시켜 봤다
