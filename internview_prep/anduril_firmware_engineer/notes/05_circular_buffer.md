# 🔄 원형 버퍼 / 링 버퍼 (Circular / Ring Buffer) — Q56~65

> 임베디드 인터뷰에서 **가장 자주 나오는 단일 자료구조**. UART/SPI 수신, 로깅,
> 센서 스트림, 오디오 DMA — 고정 크기 메모리 위에서 producer/consumer 를 잇는
> FIFO 는 전부 링 버퍼다. "구현해봐"는 기본, 진짜 난이도는 **동시성(ISR ↔ main)**
> 과 **full/empty 구분**에서 갈린다.

---

## 1. 핵심 아이디어

고정 크기 배열을 **논리적으로 원형**으로 쓴다. 두 인덱스:

- `head` (write / 생산자): 다음에 **쓸** 자리
- `tail` (read / 소비자): 다음에 **읽을** 자리

인덱스가 끝에 도달하면 0 으로 되돌아온다(**wrap-around**). 그래서 `push`/`pop`
모두 데이터 이동 없이 **O(1)**. 힙도, memmove 도 없다 — 임베디드에 완벽.

```
size = 8
 idx: 0   1   2   3   4   5   6   7
     [ ] [4] [5] [6] [7] [ ] [ ] [ ]
          ^tail            ^head
```

### wrap-around 하는 법

```c
head = (head + 1) % size;          // 일반
head = (head + 1) & (size - 1);    // size 가 2의 거듭제곱일 때 (빠름)
```

---

## 2. Full vs Empty — 인터뷰의 핵심 함정

`head == tail` 은 **비었을 때**도 **꽉 찼을 때**도 발생한다. 구분하는 3가지 방법:

| 방법 | full 판정 | empty 판정 | 용량 | 비고 |
|---|---|---|---|---|
| **① count 필드** | `count == size` | `count == 0` | `size` | 가장 직관적. 단, count 를 양쪽이 갱신하면 동시성 문제 |
| **② 한 칸 희생** | `(head+1)%size == tail` | `head == tail` | `size-1` | count 불필요 → **SPSC 무락**에 이상적 |
| **③ 자유 증가(free-running) 인덱스** | `head-tail == size` | `head == tail` | `size` | head/tail 을 마스킹 안 하고 계속 증가, 접근 시 `& mask`. count 필드 없이 전용량 |

> **면접 답변 팁**: "count 를 쓰면 간단하지만 SPSC 무락에서는 count 를 양쪽이
> 써서 경합이 생긴다. 그래서 무락 버전은 한 칸을 희생하거나(②) 자유 증가
> 인덱스(③)를 쓴다"고 말하면 깊이가 드러난다.

### ③ 자유 증가 트릭이 왜 안전한가

`head`, `tail` 을 `uint32_t` 로 계속 증가시키고 버퍼 인덱스는 `head & mask`
로만 계산한다. `count = head - tail` 은 **부호 없는 뺄셈**이라 `head` 가
2³²에서 랩되어도 정확하다(예: `head=1, tail=0xFFFFFFFF` → `count=2`). 단
`size <= 2³¹` 이어야 하고 **size 는 2의 거듭제곱**이어야 마스킹이 성립한다.

```c
bool is_pow2(uint32_t x) { return x && (x & (x - 1)) == 0; }
```

---

## 3. SPSC 무락(lock-free) — ISR ↔ main 정석 패턴 ⭐

임베디드에서 제일 중요한 그림: **UART RX ISR 이 생산자, main loop 가 소비자.**

```
   [UART 하드웨어] --IRQ--> [RX ISR: spsc_push()] --ring--> [main: spsc_pop()] --> 파싱
        생산자 1개                                    소비자 1개
```

**왜 락이 필요 없나?**

- 생산자(ISR)만 `head` 를 **쓴다**. 소비자(main)만 `tail` 을 **쓴다**.
- 서로 상대 인덱스는 **읽기만** 한다 → **write-write 경합이 구조적으로 없다.**
- 그래서 mutex 불필요. ISR 안에서 절대 락을 잡으면 안 되므로(우선순위 역전/
  데드락) 이 성질이 결정적이다.

**주의 (이식성):**

- **단일코어 베어메탈**: `head`/`tail` 을 `volatile` 로 두고 "**데이터를 먼저 쓰고
  그 다음 인덱스를 증가**" 순서만 지키면 전통적으로 충분하다. (컴파일러 재정렬만
  막으면 됨. 단일코어라 하드웨어 재정렬로 인한 문제는 없음.)
- **멀티코어(SMP)**: CPU 메모리 재정렬 때문에 `volatile` 만으로는 부족.
  **acquire/release 원자연산**(C11 `<stdatomic.h>` 또는 하드웨어 배리어)이 필요.
  - producer: `head` relaxed load(내 것) → `tail` **acquire** load(상대 것) →
    데이터 쓰기 → `head` **release** store.
  - consumer: `tail` relaxed load → `head` **acquire** load → 데이터 읽기 →
    `tail` **release** store.
  - release store 는 "데이터 쓰기가 인덱스 공개보다 먼저 보이도록" 보장하고,
    acquire load 는 짝을 이뤄 반대편이 그 순서를 관측하게 한다.

```c
// producer (한 스레드만)
uint32_t h = atomic_load_explicit(&q->head, memory_order_relaxed);
uint32_t t = atomic_load_explicit(&q->tail, memory_order_acquire);
if (((h + 1) & q->mask) == (t & q->mask)) return false;   // full (한 칸 희생)
q->buf[h & q->mask] = data;                                // 1) 데이터 먼저
atomic_store_explicit(&q->head, h + 1, memory_order_release); // 2) 인덱스 공개
```

---

## 4. MPMC — 다중 생산자/소비자 (mutex + condvar)

생산자·소비자가 **여럿**이면 `head`/`tail`/`count` 를 모두 여러 스레드가 갱신 →
무락 SPSC 트릭이 깨진다. 이때는 정직하게 **mutex 로 임계구역 보호**:

```c
void mpmc_push(mpmc_t *q, uint8_t data) {
    pthread_mutex_lock(&q->m);
    while (q->count == q->size)                 // ← if 아니라 while!
        pthread_cond_wait(&q->not_full, &q->m);
    q->buf[q->head] = data;
    q->head = (q->head + 1) % q->size;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->m);
}
```

- **`while` (not `if`)**: `pthread_cond_wait` 는 **spurious wakeup**(엉뚱한 깨어남)
  이 가능하고, 깨어난 뒤 다른 스레드가 먼저 슬롯을 채갔을 수도 있으니 조건을
  **재확인**해야 한다.
- `cond_wait` 는 대기 중 mutex 를 **자동으로 놓았다가** 깨어날 때 다시 잡는다.
- 넣은 뒤 `not_empty` 를 signal, 뺀 뒤 `not_full` 을 signal.

블로킹이 싫으면 락드(locked) but non-blocking `try_push`/`try_pop` 으로 만들 수도
있고, 진짜 고성능이면 MPMC 무락 큐(예: Michael-Scott, LMAX Disruptor)를 쓰지만
인터뷰에선 mutex+condvar 로 충분하다.

---

## 5. 오버런(overrun) 정책

버퍼가 꽉 찼는데 새 데이터가 오면?

- **거부(reject)**: `push` 가 false 반환 — 기본. 손실을 상위에서 감지.
- **덮어쓰기(overwrite)**: 가장 오래된 것을 버리고 `tail` 도 밀기 — 실시간 센서/
  오디오처럼 "최신값이 더 중요"할 때. 오버런 카운터를 두어 몇 개 잃었는지 기록.

> 드론 텔레메트리에서 링크가 느려 버퍼가 넘칠 때: 로그는 덮어쓰기 + drop 카운터,
> 제어 명령은 거부 후 에러 처리 — **데이터 종류마다 정책이 다르다**는 점을 언급.

---

## 6. 인터뷰 팔로업(follow-up) 대비

- **"volatile 이 왜 필요?"** → ISR 이 바꾸는 값을 main 이 레지스터에 캐시하지
  못하게. 단, `volatile` 은 **원자성·메모리순서를 보장하지 않는다**(멀티코어에선
  atomics 필요).
- **"count 를 안 쓰고 full/empty 구분?"** → 한 칸 희생(②) 또는 자유 증가(③).
- **"인덱스를 왜 % 대신 & mask?"** → `%` 는 나눗셈(수십 사이클), 2의 거듭제곱이면
  `& (size-1)` 한 사이클. ISR 지연 예산에 유리.
- **"size 가 2의 거듭제곱 아니면?"** → `%` 로 폴백하거나, 마스킹 변형을 못 씀.
- **"버퍼 크기 산정?"** → 최악 인입률 × 소비자 최대 지연(latency). UART 라면
  보드레이트·인터럽트 지연·main loop 주기로 계산.
- **"멀티코어에서 이 SPSC 안전?"** → acquire/release 없으면 아니오. 배리어 필요.

---

## 7. 흔한 버그 체크리스트

- [ ] `size == 0` 에서 `% size` → **0으로 나누기**. init 에서 방어.
- [ ] full 인데 덮어써서 소비자 데이터 손상.
- [ ] `head==tail` 을 무조건 empty 로 처리 → 꽉 찬 걸 비었다고 오판(count/희생칸 없을 때).
- [ ] MPMC 에서 `if (count==0) cond_wait` (while 아님) → spurious wakeup 시 빈 큐 pop.
- [ ] SPSC 에서 `count` 를 양쪽이 갱신 → 경합. (무락이라며 count 쓰면 모순)
- [ ] 인덱스 증가 후 데이터 쓰기(순서 뒤바뀜) → 소비자가 쓰레기 읽음. **데이터 먼저.**
- [ ] pop 의 `out`/`cb` NULL 미검사 → 크래시.

---

## 8. 이 문제 세트 (Q56~65)

| # | 함수 | 스타일 | 포인트 |
|---|---|---|---|
| 56 | `cb_init` | count 기반 | 초기화, NULL 방어 |
| 57 | `cb_push` | count 기반 | full 시 거부, wrap-around |
| 58 | `cb_pop` | count 기반 | FIFO, empty 시 false |
| 59 | `cb_is_empty` | count 기반 | `count==0` |
| 60 | `cb_is_full` | count 기반 | `count==size` |
| 61 | `cb_count` | count 기반 | O(1) 개수 |
| 62 | `cb_flush` | count 기반 | 인덱스 리셋 |
| 63 | `cbp2_*` | pow2 마스킹 | 자유 증가 + `& mask`, 전용량 |
| 64 | `spsc_*` | 무락 SPSC | acquire/release, ISR↔main |
| 65 | `mpmc_*` | mutex+condvar | while-wait, spurious wakeup |

---

## 9. Anduril / 드론 펌웨어 맥락

- **UART/RS422 수신**: MAVLink 유사 텔레메트리·GPS·IMU 바이트 스트림이 RX ISR
  로 들어와 링 버퍼에 쌓이고, main loop 의 FSM 파서가 꺼내 프레임을 조립한다.
  (다음 토픽인 프로토콜 파싱 FSM 과 직결.)
- **센서 → 제어 루프**: 400Hz IMU 샘플을 링 버퍼로 100Hz 제어 루프에 전달,
  레이트가 다른 producer/consumer 를 디커플링.
- **로깅/블랙박스**: 넘칠 때 덮어쓰기 + drop 카운터로 최신 비행 데이터 보존.
- **경합(contested)·저하(degraded) 환경**: 링크가 끊겨 소비자가 느려질 때 버퍼가
  어떻게 우아하게 저하(graceful degradation)되는가 — 오버런 정책 설계가 곧
  실패 모드 사고다. Anduril 이 온사이트에서 즐겨 파고드는 지점.
```
