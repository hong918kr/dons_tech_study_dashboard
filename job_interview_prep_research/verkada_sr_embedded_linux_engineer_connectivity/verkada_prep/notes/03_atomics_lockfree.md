# ⚛️ Atomic 연산 & Lock-free (Atomics & Lock-free Patterns) — Q21~30

> 리크루터 Davis의 메일이 복습 주제로 **"atomic operations"를 이름까지 찍어서** 알려줬다.
> 게이트웨이(GC31-E / GW31-E) 펌웨어에서 mutex를 쓸 수 없는 자리 — 모뎀 UART RX ISR,
> 초당 수만 번 읽히는 링크 상태, 무중단 설정 교체 — 는 전부 atomic + memory order로 푼다.
> 이 세트의 목표는 코드를 외우는 게 아니라 **"왜 이 memory order인가"를 말로 설명**하는 것이다.

---

## 1. 핵심 아이디어

동시성 버그는 두 종류다. atomic은 그 둘을 각각 다른 도구로 막는다.

| 문제 | 증상 | 도구 |
|---|---|---|
| **원자성 부족** | `c++`가 load/add/store 3단계라 증가가 유실(lost update) | `atomic_fetch_add`, CAS |
| **순서/가시성 부족** | 인덱스는 갱신됐는데 데이터가 아직 안 보임 | `memory_order` (acquire/release) |

`volatile`은 **둘 다 해결하지 못한다.** 이건 이 인터뷰에서 거의 확실히 나오는 질문이다.

```
  writer                        reader
  ────────                      ────────
  data = 42;          ①         if (load(flag, ACQUIRE))   ③
  store(flag,1,RELEASE) ②           use(data);             ④

  release(②) ─── happens-before ───> acquire(③)
  ⇒ ①은 반드시 ④보다 먼저 보인다.
```

이 그림 하나가 Q22(stop 플래그), Q25(SPSC 링), Q27/28(seqlock), Q29(refcount)를 전부 설명한다.

---

## 2. memory_order 표 — 이게 이 세트의 전부다

| order | 보장하는 것 | 언제 쓰나 | 이 세트의 예 |
|---|---|---|---|
| **relaxed** | 원자성만. 다른 메모리 접근과의 **순서 보장 없음**. 단일 변수의 modification order는 여전히 일관됨 | 순수 통계 카운터, 자기만 쓰는 인덱스 읽기, CAS 실패 경로 | Q21 `ctr_add`, Q25 `load(tail)` (생산자 자신), Q29 `rc_get` |
| **acquire** (load 전용) | 이 load **이후**의 읽기/쓰기가 앞으로 못 넘어옴. 짝이 되는 release store 이전의 모든 쓰기가 보인다 | 플래그/인덱스/포인터를 **읽어서** 데이터를 쓰려 할 때 | Q22 `stop_requested`, Q24 `spin_lock`, Q25 `ring_pop` |
| **release** (store 전용) | 이 store **이전**의 읽기/쓰기가 뒤로 못 넘어감. acquire로 이 값을 본 스레드에게 전부 보인다 | 데이터를 다 쓰고 **publish** 할 때 | Q22 `stop_request`, Q24 `spin_unlock`, Q25 `ring_push`, Q29 `rc_put` |
| **acq_rel** (RMW 전용) | acquire + release 동시에 | 읽고-쓰는 한 연산이 양쪽 다 필요할 때 (락 획득 겸 publish) | Q30 `pool_pop`의 CAS |
| **seq_cst** (기본값) | 위 전부 + **모든 seq_cst 연산에 대한 단일 전역 순서** | 확신이 없을 때의 기본값. 서로 다른 변수 두 개의 순서까지 필요할 때(Dekker 류) | 이 세트에는 필요 없음 |

### 고르는 법 (30초 규칙)

1. 이 atomic이 **다른 데이터를 publish**하는가? → store는 release, load는 acquire.
2. **값만 맞으면 되는가**(카운터)? → relaxed.
3. **모르겠는가?** → seq_cst로 시작하고 근거가 생기면 낮춘다. 면접에서 이 말은 **가점**이다.

> ⚠️ x86(TSO)에서는 acquire/release가 **공짜**(일반 mov)라 잘못 써도 안 터진다. arm64
> (Cortex-A 급 게이트웨이 SoC)는 weak memory model이라 `ldar/stlr`/`dmb`가 실제로 나간다.
> **"x86에서 됐으니 맞다"는 필드 버그의 표준 레시피다.**

### fence (`atomic_thread_fence`)

변수가 아니라 **코드 지점**에 배리어를 세운다. "매번 acquire를 거는 대신 마지막 한 번만"이
필요할 때 쓴다 → Q27 seqlock write, Q29 refcount free.
`atomic_signal_fence`는 **컴파일러 배리어일 뿐** CPU 명령이 나가지 않는다.

---

## 3. 패턴별 요점

### 3.1 카운터 (Q21)

```c
atomic_fetch_add_explicit(&c->n, 1, memory_order_relaxed);  // 반환 = 더하기 '직전' 값
```
`fetch_*`가 **이전 값**을 준다는 걸 이용하면 "슬롯 예약"도 된다(`idx = fetch_add(&tail,1)`).

### 3.2 stop 플래그 (Q22) — volatile 반례

```c
/* 틀린 코드 */
volatile int stop;
snprintf(reason, ...);   /* 비-volatile */
stop = 1;                /* 컴파일러/CPU가 위보다 먼저 보이게 할 수 있다 */
```
`volatile`이 지키는 순서는 **volatile 접근끼리**뿐이다. `reason[]` 쓰기는 자유롭게 재배치된다.
게다가 volatile은 배리어 명령을 내보내지 않으므로 arm64의 store buffer 앞에서 무력하다.

### 3.3 CAS 루프 (Q23) — 모든 lock-free의 기본형

```c
unsigned cur = atomic_load_explicit(slot, memory_order_relaxed);
while (candidate > cur) {
    if (atomic_compare_exchange_weak_explicit(slot, &cur, candidate,
            memory_order_release,   /* 성공 */
            memory_order_relaxed))  /* 실패 */
        return true;
    /* 실패하면 cur에 '현재 값'이 자동으로 들어온다 — 다시 load 하지 말 것 */
}
return false;
```

| | weak | strong |
|---|---|---|
| 가짜 실패(spurious) | 있음 | 없음 |
| 비용 | ARM LL/SC에서 더 쌈 | 내부적으로 재시도 루프 |
| 쓰는 곳 | **루프 안 (거의 항상)** | 루프가 없는 단발 CAS |

### 3.4 스핀락 (Q24) — 그리고 쓰면 안 되는 경우

```c
while (atomic_flag_test_and_set_explicit(&l->held, memory_order_acquire)) { /* spin */ }
...                                    /* 임계구역: 평범한 데이터도 안전 */
atomic_flag_clear_explicit(&l->held, memory_order_release);
```
`atomic_flag`는 C11에서 **항상 lock-free가 보장되는 유일한 타입**이다.

**쓰면 안 되는 경우 (이게 진짜 질문)**
1. 임계구역이 길거나 그 안에서 블록/시스템콜/`malloc` → CPU를 태운다. 배터리로 도는 MT81 트레일러에서 스핀은 곧 주행거리다.
2. 단일 코어 + 선점형 스케줄러 → 락 보유자가 선점되면 대기자가 타임슬라이스 전체를 태운다. 최악은 **우선순위 역전**으로 진행이 멈춘다 → mutex + priority inheritance가 정답.
3. 유저스페이스 Linux에서 경합이 있으면 대개 `pthread_mutex`가 더 빠르다(futex라 무경합은 이미 유저스페이스에서 끝남).

### 3.5 SPSC 링버퍼 (Q25/Q26)

성립 근거 한 문장: **생산자만 tail을 쓰고 소비자만 head를 쓴다 → write-write 경합이 구조적으로 없다.**

```c
r->buf[t] = b;                                                /* ① 데이터 */
atomic_store_explicit(&r->tail, next, memory_order_release);  /* ② publish */
```
- 자기 인덱스는 `relaxed`로 읽어도 된다(나만 쓰니까). 상대 인덱스는 `acquire`.
- **한 칸 비우기** → `head==tail`=empty, `next==head`=full, 용량 `CAP-1`. count 필드를 두면
  양쪽이 쓰게 되어 SPSC의 이점이 사라진다.
- `cap`이 2의 거듭제곱이면 `idx & (cap-1)` == `idx % cap`인데 나눗셈이 없다(ISR에서 중요).
  `count = (tail - head) & (CAP-1)` — **부호 없는 뺄셈**이라 랩되어도 정확.
- false sharing: 멀티코어라면 `head`/`tail`을 `_Alignas(64)`로 다른 캐시라인에.

### 3.6 seqlock (Q27/Q28) — 정직함이 점수다

```c
/* write: 홀수(쓰는 중) → fence → 데이터 → 짝수(완료) */
store(seq, v+1, relaxed); atomic_thread_fence(release);
s->data = *in;
store(seq, v+2, release);

/* read: 재시도 루프 */
for (;;) {
    before = load(seq, acquire);
    if (before & 1) continue;
    *out = s->data;                      /* 찢어진 값일 수 있다 */
    atomic_thread_fence(acquire);
    after = load(seq, relaxed);
    if (before == after) return;         /* 유효 */
}
```

**한계를 먼저 말할 것 (가산점)**
- `*out = s->data`는 C 표준상 **data race**다. TSan이 정확히 여기를 지적한다 — 버그가 아니라
  seqlock의 정의다. 실무 해법: ①필드별 relaxed atomic ②커널처럼 `READ_ONCE/WRITE_ONCE`.
  (solutions/는 TSan 빌드일 때 seqlock **스레드 테스트만** 자동으로 끄고 단일 스레드로 같은
  불변식을 검증한다. `-DVK_FORCE_SEQLOCK_THREADS`로 켜면 경고를 직접 볼 수 있다.)
- 데이터에 **포인터/핸들이 있으면 쓰면 안 된다** — 찢어진 포인터를 역참조하면 죽는다. POD 전용.
- reader starvation → 재시도 상한(`seq_try_read`) + 실패 시 mutex 경로로 fallback.
- **"그냥 mutex 쓰면 안 되나?"의 정답은 '된다, 그리고 대개 그게 맞다'**이다. seqlock은
  "읽기가 초당 수만 번이고 writer 지연이 실시간 요구를 깬다"는 **측정된 근거**가 있을 때만.

### 3.7 refcount (Q29)

```c
atomic_fetch_add_explicit(&c->refs, 1, memory_order_relaxed);   /* get */

if (atomic_fetch_sub_explicit(&c->refs, 1, memory_order_release) == 1) {  /* put */
    atomic_thread_fence(memory_order_acquire);   /* ★ 이게 없으면 use-after-free */
    free(c);
}
```
- **증가가 relaxed**: 이미 유효한 참조를 들고 있는 증가라 publish할 게 없다.
- **감소가 release**: "이제 안 본다"를 알리려면 내 접근을 먼저 flush해야 한다.
- **free 전에 acquire**: release는 "내 접근이 남에게 보인다"뿐이다. free하는 쪽은 반대로
  **"남들의 접근이 나에게 보인다"**가 필요하다.
- 참조 없이 `load(ptr)` → `fetch_add(refs)` 두 줄로 나누면 그 사이에 해제될 수 있다
  (use-after-free). 닫는 법은 셋뿐: ①짧은 락으로 두 줄 원자화 ②hazard pointer ③RCU/epoch.

### 3.8 ABA와 tagged pointer (Q30)

> T1이 `top == A`를 읽고 선점된 사이, T2가 A를 pop, B를 pop, A를 다시 push한다.
> top은 다시 A지만 **A->next는 완전히 다른 값**이다. T1의 `CAS(top, A, A->next_old)`는
> "값이 같은가"만 보므로 **성공해버리고** 리스트가 깨진다.

해법 3가지:
1. **tagged pointer** — (포인터 \| 버전 카운터)를 한 워드로 묶어 CAS. push/pop마다 tag++.
   64비트 CAS로 `(32bit index | 32bit tag)`, 또는 DWCAS(x86 `cmpxchg16b`, ARM `CASP`).
2. **hazard pointer / epoch / RCU** — 재사용 자체를 지연시킨다.
3. **그냥 mutex** — 임베디드 코드 대부분에서 이게 정답이다.

포인터 대신 **배열 인덱스**를 쓰면 32비트가 남아 태그를 공짜로 얻는다(Q30 구현이 이 방식).

---

## 4. 흔한 함정 (인터뷰 감점 포인트)

1. **`volatile`로 충분하다고 답하기** — 즉사. 원자성도 순서도 보장하지 않는다.
2. **CAS 실패 후 다시 `atomic_load`** — 실패 시 expected에 현재 값이 이미 실려 있다.
3. **seqlock read를 `do/while`로 쓰기** — `continue`가 while 조건으로 점프해서 **아직 대입된 적
   없는 `after`(쓰레기값)**와 비교한다. 운 나쁘면 `*out`을 한 번도 채우지 않고 리턴한다.
   → `for (;;)` + 명시적 `return`. (흔한 예제 코드에 실제로 박혀 있는 버그)
4. **seqlock write에서 첫 fence를 빼기** — reader가 '짝수 seq + 반쯤 쓰인 데이터'를 보고 통과한다.
5. **refcount free 전에 acquire fence 없음** — release만으로는 남의 접근을 못 본다.
6. **SPSC에 count 필드 추가** — 양쪽이 쓰게 되어 무락이 깨진다.
7. **`is_pow2(0)`이 true** — `0 & (0-1) == 0`. `x != 0 &&`를 반드시 붙인다.
8. **가득 찬 링에서 ISR이 재시도/대기** — 인터럽트를 막는다. **drop하고 카운터를 올린다.**
9. **drop/실패를 조용히 삼키기** — 필드에서 가장 추적하기 어려운 버그다. 반드시 센다.
10. **근거 없이 lock-free 선택** — "측정했더니 mutex가 병목이었다"가 없으면 유지보수 비용만 오른다.

---

## 5. 면접에서 말할 것 (한국어 + English)

**① volatile vs atomic**
- KO: "`volatile`은 최적화 억제일 뿐, 원자성도 메모리 순서도 보장하지 않습니다. 공유 상태에는 `_Atomic`과 명시적 memory order를 씁니다."
- EN: *"`volatile` only stops the compiler from caching a load — it guarantees neither atomicity nor ordering. For shared state I use `_Atomic` with an explicit memory order."*

**② acquire/release 한 문장 정의**
- KO: "release store 이전의 모든 쓰기는, 그 값을 acquire load로 본 스레드에게 반드시 보입니다. 그 한 쌍이 happens-before를 만듭니다."
- EN: *"Everything written before a release store becomes visible to any thread that acquire-loads that value. That pair is what creates the happens-before edge."*

**③ 왜 relaxed인가**
- KO: "이 카운터는 다른 데이터를 publish하지 않고 값만 맞으면 되므로 relaxed로 충분합니다. 배리어 비용을 낼 이유가 없습니다."
- EN: *"This counter doesn't publish any other data — only the count has to be right — so relaxed is sufficient and I don't pay for a barrier."*

**④ SPSC가 무락인 이유**
- KO: "생산자만 tail을 쓰고 소비자만 head를 씁니다. write-write 경합이 구조적으로 없으니 남는 문제는 순서뿐이고, 그건 release/acquire 한 쌍으로 끝납니다."
- EN: *"Only the producer writes tail and only the consumer writes head, so there is no write-write contention by construction. The only remaining problem is ordering, and one release/acquire pair solves it."*

**⑤ 스핀락을 안 쓰는 이유**
- KO: "임계구역이 길거나 단일 코어에서 선점되면 스핀락은 CPU를 태우고 우선순위 역전을 만듭니다. 그런 경우엔 mutex에 priority inheritance를 씁니다."
- EN: *"If the critical section is long, or on a single core where the holder can be preempted, a spinlock just burns CPU and invites priority inversion. There I'd use a mutex with priority inheritance."*

**⑥ seqlock의 한계를 먼저 말하기**
- EN: *"To be upfront: a seqlock is technically a data race under the C memory model — ThreadSanitizer will flag the payload copy. The real-world fixes are per-field relaxed atomics, or the kernel's `READ_ONCE`/`WRITE_ONCE`. And it's POD-only: never put a pointer in the payload."*

**⑦ trade-off를 아는 시니어로 들리게**
- KO: "기본은 mutex + condition variable입니다. lock-free는 측정된 근거가 있을 때만 씁니다 — 없으면 유지보수 비용만 올라갑니다."
- EN: *"My default is a mutex and a condition variable. I only reach for lock-free when I have a measurement that says the lock is the bottleneck — otherwise it's just maintenance cost."*

**⑧ ABA**
- EN: *"CAS only compares the value, not the history. If a node is popped and pushed back, a stale CAS succeeds and corrupts the list. I'd pack a version tag next to the pointer, or avoid reclamation entirely with hazard pointers or RCU."*

---

## 6. 체크리스트

- [ ] `volatile`이 보장하는 것/못 하는 것을 3가지로 나눠 말할 수 있다
- [ ] relaxed / acquire / release / acq_rel / seq_cst를 각각 "언제 쓰나"로 설명할 수 있다
- [ ] release store ↔ acquire load가 happens-before를 만든다는 문장을 영어로 말할 수 있다
- [ ] CAS 루프를 보지 않고 쓸 수 있고, 실패 시 expected가 갱신된다는 걸 안다
- [ ] weak vs strong의 차이와 각각의 사용처를 안다
- [ ] SPSC 링버퍼를 5분 안에 쓰고 "왜 무락인지" 한 문장으로 설명한다
- [ ] 한 칸 비우기 / pow2 마스킹 / unsigned 뺄셈 count를 이유와 함께 말한다
- [ ] seqlock write/read를 쓰고, **data race라는 한계를 먼저** 말한다
- [ ] seqlock read를 `for(;;)`로 쓴다 (`do/while` + `continue` 함정을 안다)
- [ ] refcount에서 감소=release, 마지막에 acquire fence인 이유를 설명한다
- [ ] ABA 시나리오를 3문장으로 설명하고 해법 3가지를 댄다
- [ ] "그냥 mutex 쓰면 안 되나?"에 **"됩니다, 그리고 대개 그게 맞습니다"**로 시작한다
- [ ] TSan(`-fsanitize=thread`)으로 검증하는 습관을 언급한다
