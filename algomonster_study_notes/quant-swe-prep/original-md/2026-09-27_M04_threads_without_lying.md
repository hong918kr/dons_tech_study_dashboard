# Module 4 — Handle Threads Without Lying To Yourself (스레드를 정직하게 다루기)

> 출처: AlgoMonster Quant SWE Interview Prep, Module 4 (레슨 6개) · 한국어 개념 정리 노트
> 목표: 스레드 hot path를 **"무엇이 공유되나 → 접근이 설계상 race-free이거나 동기화됐음을 증명 → 그다음에 비용"** 순서로 추론하기. **정확성 먼저, 속도는 그다음.**

## 한눈에 보기 (5분 복습용)

| 도구 | 해결하는 것 | 비용 | 언제 |
|---|---|---|---|
| `std::mutex` + `lock_guard` | 불변식(invariant) 보호, check-then-act 묶기 | 비경합 ~수십 ns / **경합 시 커널 sleep·wake µs** + 직렬화 | 일반적인 공유 쓰기. 임계구역은 최소로 |
| `condition_variable` | "일이 생길 때까지 잠자기" (spin/nap 둘 다 회피) | notify→실행까지 커널 경유 **µs** | 사람 속도 파이프라인, 로깅, 리스크 리포트 |
| `std::atomic` | 단일 위치 RMW를 분할 불가로 | 비경합 ≈ 일반 연산 / **경합 시 캐시 라인 핑퐁** | 카운터, 플래그, 포인터 하나 발행 |
| acquire/release | **서로 다른 위치 간 순서** (데이터 → 플래그 발행) | 일부 HW에서 seq_cst보다 쌈 | publish/subscribe 핸드오프 |
| SPSC ring | 락·sleep 없는 핸드오프 (인덱스마다 writer 1명) | 아이템당 원자 인덱스 갱신 2번, 수십 ns | **tick-to-trade 경로** |
| CAS 루프 | 다중 writer 갱신 | 경합 시 재시도 | lock-free MPMC (직접 짜지 말고 라이브러리) |

**공유 순위 (외울 것):** 공유 안 함 > 읽기만 공유 > 작은 원자 하나 공유 > 작은 임계구역 > 큰 락.

---

## 1. Threads, Shared State, Data Races

**상황:** 거래소 피드 추가로 업데이트율 2배 → vector를 반으로 나눠 스레드 2개 + 공유 카운터. 1천만 건 넣었더니 5,027,405 / 다음엔 5,034,595. 크래시·예외·경고 **없이** 매번 다른 오답.

### 스레드가 공유하는 것
- 스레드마다 **자기 스택**(지역 변수 private) + 코드 위치.
- **힙, 전역, 포인터/참조로 닿는 모든 것은 공유.** 공유가 협업의 수단이자 문제의 전부.
- 스케줄러가 **아무 두 명령 사이에서나** 스레드를 멈출 수 있음 — 루프 중간, 식 중간, `++`의 중간.

### `++`은 세 단계
1. load (메모리 → 레지스터) 2. add (레지스터) 3. store (레지스터 → 메모리)
- 카운터 41: A가 41 load 후 멈춤 → B가 41 load·add·store 42 → A 재개, 레지스터의 41+1 = 42 store. **두 번 증가했는데 한 번만 반영**, 아무도 기록하지 않음.
- 비유: 화이트보드 집계를 하는 두 점원 — 둘 다 41을 읽고 머릿속으로 계산해서 42를 씀. 산수는 맞았고 **스케줄이 틀렸다.**

### Data race = UB
- 정의: 두 스레드가 **같은 메모리 위치**에 접근 + **적어도 하나가 쓰기** + **동기화 없음** → 프로그램 전체 동작이 **정의되지 않음**. "값이 stale일 수 있다"가 아니라 규칙이 더 이상 프로그램을 구속하지 않는다는 뜻.
- 이유: 컴파일러는 data race가 없다고 **가정하고 최적화**. 동기화 없는 루프 증가는 레지스터에 두고 끝에 한 번 store 가능 → 작성하지 않은 코드로 변형됨. (데모가 `noinline`을 붙인 이유)
- 검출: **`-fsanitize=thread` (TSan)** — race 발생 시 두 스택 트레이스 보고. 천 번 통과하고 천한 번째에 실패하는 코드를 **증상이 아니라 race 자체로** 잡음.

### Data race ≠ Race condition
| | Data race | Race condition |
|---|---|---|
| 종류 | **접근** 결함 (기계적) | **순서** 의존 로직 결함 |
| 예 | 동기화 없는 카운터 증가 | 리스크 체크: 두 스레드가 "포지션 < 한도면 주문" → 둘 다 통과 후 한도 초과 |
| data race 없이 존재? | — | **가능** (모든 접근이 완벽히 동기화돼도) |
| 수정 | 접근을 동기화 | **check와 act를 한 단계로** 로직 재설계 |

- ✅ 체크포인트: 41에서 두 스레드가 `++` 한 번씩 → **42 또는 43** (인터리빙에 따라).
- 🎤 트레이딩에서 카운터는 카운터가 아니다 — 포지션, 리스크 한도 대비 주문 수, 오더북 무결성을 가르는 시퀀스 번호. 공유를 안전하게 안 만든 병렬 프로그램은 빠른 게 아니라 **"좋은 레이턴시 숫자를 가진 고장난 프로그램"**.

---

## 2. Mutexes, Lock Scope, Contention

**핵심:** mutex = 최대 한 스레드만 잡는 락. `lock()`이 막히면 대기, `unlock()`으로 해제. 사이 코드 = **임계구역(critical section)**: 같은 mutex의 임계구역은 동시에 두 개가 돌 수 없음.

```cpp
std::mutex m; long long updates_handled = 0;
for (...) {
    apply_update(updates[i]);
    std::lock_guard<std::mutex> lock(m);   // RAII: 모든 경로에서 unlock
    ++updates_handled;
}
```

### 락은 줄이 아니라 **불변식**을 보호한다
- mutex는 **관례**지, 방어막이 아니다. 해당 변수를 만지는 **모든 경로**가 락을 잡기로 합의했기 때문에 보호되는 것. 락 없이 증가하는 경로 하나만 추가돼도 race 복귀 — 컴파일러는 경고 안 함 (변수와 mutex는 C++ 입장에서 남남).
- 여러 필드에 걸친 불변식 → 한 번의 락 홀드에 묶기 = race condition 해소:
```cpp
std::lock_guard<std::mutex> lock(risk_m);
if (position + update.qty <= limit) {   // check...
    send_order(update);
    position += update.qty;             // ...and act, 한 쌍으로 원자적
}
```

### `lock()`의 비용
- **비경합:** 획득·해제 각각 특수 CPU 연산 1개 수준, 합쳐 수십 ns, **커널 근처도 안 감**. cold 코드의 mutex는 두려워할 것 없음.
- **경합:** 잠깐 spin → 포기하고 OS에 sleep 요청 → 깨어남. 커널 왕복 = **µs** (비경합의 수천 배). 경합된 `lock()`은 루프에서 가장 비싼 줄이 될 수 있음.
- **더 깊은 비용 = 대기 자체.** 임계구역은 겹칠 수 없으므로 그 구간 동안 4스레드 프로그램 = 큐잉 오버헤드 붙은 1스레드 프로그램. 락은 사이클을 태워서가 아니라 **겹침을 금지해서** 느리다.

### 임계구역을 작게
- 패턴: **일은 밖에서, 락은 결과 발행만.** 예: 업데이트마다 락(1천만 번) ❌ → 스레드별 지역 합계(private이라 race-free) + 끝에 한 번 락으로 병합 ✅.
- 안티패턴: 락 잡고 느린 일. 임계구역 안 할당 → allocator slow path(페이지 폴트 포함) 동안 모두 대기. 로깅은 더 나쁨(디스크). **락 안에서 할당·로그·I/O 금지.**

### Deadlock
- 스레드1: AAPL 락 → MSFT 대기 / 스레드2: MSFT 락 → AAPL 대기 → 서로가 필요한 락을 쥐고 **영원히 대기**. 예외·크래시 없음.
- 예방: **전역 락 순서** (예: 티커 알파벳 순) → 순환 대기 불가.
- 표준: `std::scoped_lock lock(m1, m2);` — 데드락 회피 알고리즘으로 여러 mutex 동시 획득, 소멸자에서 전부 해제.
- 최선: **두 락을 동시에 쥐지 않는 구조.**

- ✅ 체크포인트: 공유 vector에 fill 추가(뜨거운 mutex) + 로그 파일 쓰기 → 임계구역엔 **`push_back`만**, 로그는 unlock 후 포맷·기록.
- 🎤 "정확한데도 매 증가마다 mutex가 느린 이유?" → 임계구역이 겹칠 수 없어서 병렬 부분이 순차 큐로 되돌아감.
- 🎤 "데드락 예방?" → 전역 락 순서, `std::scoped_lock`, 구조적으로 두 락 동시 보유 금지.

---

## 3. Condition Variables & Producer-Consumer

**상황:** 피드 = 스트림. 네트워크 스레드가 큐에 push, 전략 스레드가 pop. 문제는 보호가 아니라 **큐가 비었을 때 소비자는 뭘 하나?**

### 나쁜 대기 두 가지
- **Spin:** 계속 lock→empty 확인→unlock. ns 반응, 대신 **코어 하나를 하루 종일 태움**.
- **Nap:** 비었으면 1ms sleep. 코어는 쉬지만 체크 직후 도착한 업데이트가 **최대 1ms 방치** (µs 시장에서 영원).
- 원하는 것: 타이머 없이 자고, **생산자가 일 생기는 순간 깨워줌**.

### `std::condition_variable`
- 소비자 `wait()` = 타이머 없는 sleep, 생산자 `notify_one()` = 잠든 대기자 하나 깨움.
- **`wait(lock)`의 춤:** ① mutex 해제 ② sleep ③ 깨면 mutex 재획득 ④ 반환. **①②는 원자적** → 생산자가 그 틈에 push+notify를 끼워넣을 수 없음. (락 쥐고 자면 생산자가 push 못 해 데드락)
- 그래서 **`unique_lock`** 사용: `lock_guard`는 스코프 중간에 해제·재획득 불가, `unique_lock`은 가능.

```cpp
// consumer
std::unique_lock<std::mutex> lock(m);
while (q.empty()) cv.wait(lock);       // if가 아니라 while!
MarketUpdate u = q.front(); q.pop_front();
lock.unlock();
apply_update(u);                       // 처리는 락 밖에서

// producer
{ std::lock_guard<std::mutex> lock(m); q.push_back(u); }  // 상태 변경은 mutex 아래
cv.notify_one();                                          // 그다음 벨 울리기
```

### Predicate가 진실이다 (`while`인 이유)
1. **Spurious wakeup** — notify 없이도 `wait()`이 반환될 수 있음 (금지하면 모든 플랫폼 구현이 느려져서 표준이 허용).
2. **소비자 여럿** — 다른 스레드가 먼저 깨어 아이템을 가져감 → 깨어났는데 빈손.
3. **Lost wakeup (반대편 함정)** — 생산자가 mutex 없이 상태를 바꾸면: 소비자가 empty 확인 → sleep 직전에 멈춤 → 그 틈에 notify 발사 → 이미 울린 벨을 기다리며 영원히 잠.
- 소비자 규율: **깰 때마다 실제 상태 재확인.** 생산자 규율: **같은 mutex 아래서 상태 변경 → 그다음 notify.** 둘 다 지키면 wakeup이 어떻게 오작동해도 정확. **notify는 힌트, predicate가 진실.**
- `cv.wait(lock, [&]{ return !q.empty(); });` — predicate 오버로드가 루프를 대신 써줌.

### 비용
- notify → 소비자 실제 실행 = 커널이 잠든 스레드를 깨우는 왕복 **µs** (원하는 코어가 바쁘면 더).
- 기다림이 괜찮은 대부분의 시스템(주문 화면, 리스크 리포트, 로깅)엔 정답. **tick-to-trade 경로는 예외** → 절대 자지 않는 큐 필요 (다음: atomic → SPSC).

- ✅ 체크포인트: 생산자가 push+`notify_one()`을 소비자가 wait에 도달하기 **직전에** 함 → 소비자는 wait 전에 predicate를 확인, 아이템을 보고 **아예 잠들지 않음**.
- 🎤 "생산자가 notify 전에 mutex를 잡는 이유?" → 상태 변경이 mutex 밖이면 소비자가 확인하고 변경을 놓친 뒤 notify를 자면서 흘려버림. lock → 변경 → unlock → notify.

---

## 4. Atomics & Memory Ordering Intro

### Atomic = RMW를 분할 불가로
- `fetch_add(1)` = load·add·store를 **하드웨어가 한 단위로**. 끼어들 순간이 없음. mutex·임계구역·sleep 없음.
- `std::atomic<T>` 연산: `load`, `store`, `fetch_add`, `exchange`, `compare_exchange` (CAS).

### 비경합은 싸다, 공유가 비싸다
- 한 스레드만 만지는 atomic ≈ 일반 연산 비용.
- 두 코어가 같은 atomic에 계속 쓰면: 최신 사본은 한 코어만 소유 가능 → **쓰기마다 소유권이 코어 간 이동** (캐시 라인 핑퐁). 명령이 느려진 게 아니라 **값이 코어 사이를 여행**. (프로토콜 = Module 5 MESI)
- 데모 사다리: 단독 atomic (쌈) → 2스레드 한 카운터 (비쌈) → 스레드별 카운터 (비용 사라짐).
- **Atomic은 공유를 싸게 만들지 않는다. 락 없이 정확하게 만들 뿐.** 빠른 설계 = 최소 공유 → SPSC로 이어짐.

### 분할 불가 ≠ 순서
```cpp
MarketUpdate shared_update; std::atomic<bool> ready{false};
// producer: (1) shared_update = latest;  (2) ready.store(true);
// consumer: (3) while (!ready.load()) {} (4) process(shared_update);
```
- `ready`가 atomic이면 반쯤 쓰인 플래그는 안 봄. 하지만 **`ready == true`를 봤을 때 `shared_update` 쓰기도 보인다는 보장은 별개.** 컴파일러·CPU가 독립 메모리 연산을 재배치 가능 → 플래그는 보이는데 데이터는 stale.
- 분할 불가성 = **한 위치** 보호. 순서 = **여러 위치의 쓰기가 다른 스레드에 보이는 순서**에 대한 약속 — **따로 요청해야 함.**

### Acquire / Release = 발행 / 구독
```cpp
// producer
shared_update = latest;
ready.store(true, std::memory_order_release);        // 위의 모든 쓰기를 발행
// consumer
while (!ready.load(std::memory_order_acquire)) {}    // 성공 시 구독
process(shared_update);                              // latest가 보장됨
```
- release store 이전의 모든 쓰기 → 그 값을 읽은 acquire load 이후에 **보장된 가시성**. 단방향 게이트.

### 메모리 오더 3종
| 오더 | 보장 | 언제 |
|---|---|---|
| `seq_cst` (기본) | 모든 atomic 연산이 **하나의 전역 순서**로 보임 | 모르겠으면 이것. `fetch_add(1)` 인자 없을 때 |
| `acquire`/`release` | 발행·구독 쌍의 순서 | 데이터 + ready 플래그, SPSC 인덱스 |
| `relaxed` | **분할 불가성만**, 순서 없음 | 다른 쓰기가 의존하지 않는 독립 통계 카운터 |
- 흔한 실수: **기본값처럼 relaxed를 쓰는 것.**

- ✅ 체크포인트: 구조체 쓰고 `ready.store(true)`, 소비자는 `ready.load()` spin — 둘 다 relaxed면 왜 깨지나? → relaxed는 각 연산의 분할 불가만 보장하고 **구조체 쓰기가 플래그 쓰기보다 먼저 보인다는 건 보장 안 함** → stale 구조체.
- 🎤 "버퍼 + ready 플래그, 어떤 오더로?" → 플래그 store에 **release**, 플래그 load에 **acquire**.

---

## 5. SPSC Ring Buffer Handoff

**핵심:** 피드 스레드 → 전략 스레드 핸드오프 = 시스템에서 가장 레이턴시 민감한 지점. mutex(직렬화 + 커널 park)도, CV(µs wake)도 병목. **락도 sleep도 없는 큐**를 atomic + acquire/release 위에 짓는다.

### Ring buffer
```cpp
template <typename T, std::size_t CAP>
struct SpscRing {
    T buf[CAP];                       // 고정 저장소
    std::atomic<std::size_t> head;    // 소비자가 다음에 읽을 슬롯
    std::atomic<std::size_t> tail;    // 생산자가 다음에 쓸 슬롯
};
```
- head=2, tail=5 → 슬롯 2,3,4가 대기 중. 인덱스는 끝에서 modulo로 0으로 wrap.
- **empty:** `head == tail` / **full:** tail을 전진시키면 head에 닿을 때 (슬롯 하나는 비워둠).
- 연속 배열 → 캐시 친화, 한 번 할당 → hot path allocator 0.

### 인덱스마다 writer 1명 = 락이 필요 없는 이유
- **SPSC = 정확히 한 스레드만 push, 한 스레드만 pop.** 제약이 아니라 **락을 없애는 장치**.
- `tail`은 생산자만 쓰고, `head`는 소비자만 씀. 두 writer가 한 위치에 쓰는 일이 없으니 RMW race 자체가 존재하지 않음. CAS도 불필요 (CAS는 한 위치의 두 writer를 중재하는 용도).
- 각자 상대 인덱스를 **읽어서** 여유/일을 파악하고, **자기 것만 씀**.

### Release/acquire로 슬롯 발행
```cpp
bool push(const T& v) {                                        // producer only
    std::size_t t = tail.load(std::memory_order_relaxed);      // 내 인덱스: relaxed
    std::size_t next = (t + 1) % CAP;
    if (next == head.load(std::memory_order_acquire)) return false;  // full
    buf[t] = v;                                                // 슬롯 쓰기...
    tail.store(next, std::memory_order_release);               // ...그다음 발행
    return true;
}
bool pop(T& out) {                                             // consumer only
    std::size_t h = head.load(std::memory_order_relaxed);      // 내 인덱스: relaxed
    if (h == tail.load(std::memory_order_acquire)) return false;     // empty
    out = buf[h];                                              // 발행된 슬롯 읽기
    head.store((h + 1) % CAP, std::memory_order_release);      // 슬롯 반납 발행
    return true;
}
```
- 규칙: **자기 인덱스는 relaxed로 로드, 상대 인덱스는 acquire, 자기 인덱스 발행은 release.** 자기 자신에 대한 순서 보장은 필요 없고, 상대의 발행에 대해서만 필요.
- 동기화 예산 = 아이템당 원자 인덱스 갱신 2번, 커널 호출 0. 아이템당 **수십 ns** (CV wake보다 수 자릿수 작음).
- (실무 보강: `CAP`을 2의 거듭제곱으로 두면 `% CAP` → `& (CAP-1)`; head/tail을 서로 다른 캐시 라인에 `alignas(64)` — false sharing은 Module 5)

### Spin or sleep
- 비었을 때 소비자는 **busy-wait(spin)** — CV와 정반대 결정. 코어를 100% 점유하지만 도착 시 **ns 반응**, 커널 wake 없음.
- 거래: **물리 코어 하나를 그 스레드에 전용 + 핀닝**, 영원히 100% busy 수용. 트레이딩 회사에선 코어가 µs보다 쌈.
- 그 외 모든 곳에선 CV 모델(자고 깨워지기)이 정답. **spin은 메스지 기본값이 아니다.**
- Module 1 정상 상태 원칙의 완성형: 메모리는 시작 시 확보, 두 스레드는 락·syscall 없이 전속력.

- ✅ 체크포인트: SPSC는 mutex 불필요, 생산자 둘이면 필요한 이유 → 생산자 하나면 `tail`의 writer가 하나라 RMW race 없음. 둘이면 둘 다 `tail`에 써서 갱신 유실.
- 🎤 "두 스레드 간 시장 데이터를 최저 레이턴시로?" → SPSC ring: 고정 배열, head/tail, 락·할당 없음, 소비자 spin. 인덱스마다 writer 하나라 CAS 불필요, release로 발행·acquire로 읽기.

---

## 6. Lock-Free Queue Concepts

**문제:** 피드 스레드 둘이 같은 큐에 push → 둘 다 `tail==5` 읽고, 둘 다 슬롯 5에 쓰고, 둘 다 `tail=6`. 아이템 하나 덮어쓰기 + 전진 하나 유실. "tail 읽고 tail+1 쓰기"는 여전히 분리 가능한 두 단계 → **다중 writer엔 다른 primitive.**

### Compare-and-swap (CAS)
- `compare_exchange_weak(expected, desired)` — 한 번의 분할 불가 단계로: 현재값 == expected면 desired로 바꾸고 성공, 아니면 그대로 두고 **실제 현재값을 expected에 로드** 후 실패.
- = race condition이 쪼개놓는 **check-then-act를 한 원자 동작으로.**
- 실패가 유용한 이유: **이긴 값을 돌려줌** → 현실 기준으로 재계산 후 재시도.

### 재시도 루프 (모든 lock-free 알고리즘의 심장)
```cpp
void add(long long v) {
    long long cur = total.load(std::memory_order_relaxed);
    while (!total.compare_exchange_weak(cur, cur + v)) {
        // cur가 실제 현재값으로 갱신됨 → 재시도
    }
}
```
- 락을 쥐는 스레드도, 막히는 스레드도 없음. 경합 시 여러 번 루프 = 비용은 **대기 대신 재시도**.
- **실패한 CAS = 다른 누군가의 CAS가 성공** → 시스템 전체는 항상 전진.
- (`_weak`는 spurious failure 허용 → 루프 안에서 쓰는 게 정석)

### "Lock-free"의 정확한 의미 — 진행 보장 사다리
| 등급 | 보장 |
|---|---|
| **Blocking** (mutex 기반) | 한 스레드가 나쁜 순간에 멈추면 전부 멈출 수 있음 (락 홀더가 임계구역 중 선점되면 모두 대기) |
| **Lock-free** | **시스템 전체**는 항상 진행: 어떤 스레드가 멈춰도 최소 한 스레드는 유한 단계 안에 완료 |
| **Wait-free** | **모든 스레드**가 유한 단계 안에 완료, 기아 없음 |
- CAS 루프 대부분은 lock-free지만 wait-free는 아님 (운 나쁜 스레드는 계속 질 수 있음).
- 핵심: **멈춘 스레드가 쥐고 있을 락이 없다.**

### Lock-free는 속도 도구가 아니라 **예측 가능성** 도구
- ❌ "lock-free라 빠르다." 저경합에선 mutex가 같거나 더 빠름, 고경합 CAS는 재시도로 사이클을 태움.
- ✅ 사는 것 = **bounded tail.** 경합 락의 커널 sleep(µs), 락 홀더가 OS에 선점되면 모두 멈춤 → 이 실패 모드 제거. **평균은 가끔 나빠지고 최악은 좋아짐** = p99/p99.9를 지키는 거래.

### ABA 문제
- 포인터를 A로 읽음 → CAS 전에 다른 스레드가 A pop, 다음 노드 pop, 재활용 노드를 **같은 주소 A**에 push → 내 CAS는 A를 보고 성공 — 구조는 바뀌었다 돌아왔는데 "안 바뀜"으로 오인. ("같지 않음"과 "같음"은 다르다)
- 해결: 포인터에 **버전 태그** 붙이기 (옛 태그 A ≠ 새 태그 A), 또는 **hazard pointer** 같은 안전 회수 기법.
- (Module 2의 generational handle과 같은 아이디어)

### 직접 짜지 마라
- `is_lock_free()`: CPU가 한 명령으로 swap 가능한 작은 타입만 true. 큰 타입은 라이브러리가 **몰래 락으로 대체** → "lock-free" 구조가 사실 락 → **확인하라.**
- 올바른 MPMC lock-free 큐(ABA 처리 + 안전 회수)는 작성도 증명도 어렵다 → `boost::lockfree`, moodycamel, folly.
- 사다리: **공유 안 함 → 1:1이면 SPSC ring → 다중 writer면 검증된 라이브러리.** 범용 lock-free 큐를 손으로 짜면 "테스트에선 안 나오고 프로덕션에서 하루 한 번" 버그를 출하함.

- ✅ 체크포인트: "큐를 lock-free로 만들었으니 mutex보다 빠르다" 교정 → lock-free는 진행을 보장하고 최악을 제한하지만 저경합에선 mutex가 같거나 빠름. **이득은 짧은 꼬리지 낮은 평균이 아님.**
- 🎤 "직접 lock-free 큐를 짜겠나?" → SPSC는 예(단순). 다중 producer는 아니오 — ABA·회수 때문에 `boost::lockfree`/moodycamel. (ABA를 묻기 전에 먼저 언급하면 실전 경험 신호)

---

## 용어 사전

| 용어 | 한 줄 정의 |
|---|---|
| Data race | 같은 위치, 최소 하나 쓰기, 동기화 없음 → UB |
| Race condition | 결과가 사건 순서에 의존하는 로직 결함 (check-then-act) |
| TSan | `-fsanitize=thread`, 런타임 data race 검출기 |
| Critical section | 같은 mutex 아래 한 번에 한 스레드만 실행되는 구간 |
| Invariant | 여러 필드에 걸쳐 항상 참이어야 하는 명제 — 락이 진짜 보호하는 대상 |
| Contention | 여러 스레드가 같은 락/위치를 동시에 원함 |
| Deadlock | 순환 대기로 모두 영원히 블록 |
| `scoped_lock` | 여러 mutex를 데드락 회피 알고리즘으로 동시 획득 |
| Spurious wakeup | notify 없이 wait이 반환되는 것 (표준 허용) |
| Lost wakeup | 상태 변경을 mutex 밖에서 해서 notify를 놓치고 영원히 잠듦 |
| `unique_lock` | 스코프 중간 해제/재획득 가능한 RAII 락 |
| RMW | read-modify-write |
| Cache-line ping-pong | 경합 atomic의 라인 소유권이 코어 간 왕복 |
| Acquire / Release | 구독 load / 발행 store, 쌍으로 교차 위치 순서 보장 |
| seq_cst | 모든 atomic 연산의 단일 전역 순서 (기본) |
| Relaxed | 분할 불가만, 순서 없음 |
| SPSC | single-producer single-consumer |
| Busy-wait / spin | sleep 없이 반복 확인 (코어 전용 + 핀닝과 세트) |
| CAS | compare-and-swap, `compare_exchange_weak/strong` |
| Lock-free / Wait-free | 시스템 전체 진행 보장 / 모든 스레드 진행 보장 |
| ABA | A→B→A 변화를 CAS가 "변화 없음"으로 오인 |
| Hazard pointer | lock-free 구조의 안전한 메모리 회수 기법 |
