# Module 2 — Write Safe C++ (안전한 C++ 쓰기)

> 출처: AlgoMonster Quant SWE Interview Prep, Module 2 (레슨 8개) · 한국어 개념 정리 노트
> 목표: 수명(lifetime)·소유권(ownership)·view·move·람다 캡처·컨테이너 무효화·캐스트를 **컴파일러가 실제로 만드는 코드와 메모리** 수준에서 설명하기.

## 한눈에 보기 (5분 복습용)

| 주제 | 한 줄 요약 | 면접 키 문장 |
|---|---|---|
| 객체 수명 | 스코프 끝 = 파괴, **역순(LIFO)**. 임시 객체는 **세미콜론**에서 죽음 | "댕글링 포인터는 바로 안 터진다. 메모리를 지우지 않고 `%rsp`만 움직이니까" |
| RAII | 생성자에서 획득, 소멸자에서 해제 → **모든 탈출 경로**에서 정확히 1회 | "-O2에서 손으로 짠 코드와 같은 어셈블리. `-fno-exceptions`에서도 필수" |
| 스마트 포인터 | `unique_ptr` 8B·오버헤드 0 / `shared_ptr` 16B + **원자적 refcount** | "읽기만 하면 `const T&`. `shared_ptr` by-value는 캐시 라인 핑퐁" |
| View | `string_view`/`span` = {ptr, len} 16B, **소유 안 함** | "by-value로 넘겨라(레지스터 2개). 임시·재할당·반환에서 댕글링" |
| Move | `std::move`는 **명령어 0개**인 캐스트. flat struct의 move = copy | "move 생성자에 `noexcept` 없으면 vector가 **복사로 폴백**" |
| 람다 | 캡처 = 익명 struct의 멤버. `[&]` = 포인터 | "비동기 콜백엔 값/`std::move` 캡처. 멤버 캡처는 `this` 캡처" |
| 무효화 | vector 재할당 → 전부 댕글링 / unordered_map rehash → **iterator만** 깨짐 | "`reserve`, 인덱스, **generation handle**로 방어" |
| 캐스트 | static(컴파일타임) · dynamic(RTTI 런타임) · reinterpret/const(명령 0개) | "hot loop에서 dynamic_cast 체인은 안티패턴 → virtual/variant" |

---

## 1. Object Lifetime — 객체는 언제 시작하고 끝나나

**핵심:** 함수가 반환해도 CPU는 메모리를 지우지 않는다 — 스택 포인터만 올린다(`add $0x20, %rsp`). 그래서 댕글링 포인터는 테스트에선 멀쩡하다가, **다른 호출/스레드가 그 바이트를 덮어쓸 때** 프로덕션에서 터진다.

```cpp
const char* active_symbol = nullptr;
void handle_message(const MessageBuffer& buf) {
    std::string symbol = parse_symbol(buf);
    active_symbol = symbol.c_str();   // 내부 버퍼(SSO면 스택, 아니면 힙)를 가리킴
}   // ~string(): 힙 해제 또는 스택 프레임 폐기 → active_symbol 댕글링 (UB)
```

### 스택 객체와 LIFO 파괴
- 생성 = 선언 줄, 파괴 = 닫는 중괄호. 여러 지역 변수는 **생성의 역순**으로 파괴.
- 이유: **의존성 안전.** 나중에 만든 객체가 먼저 만든 자원에 의존(예: `LockGuard guard(mutex)`). mutex가 먼저 죽으면 guard 소멸자가 죽은 상태를 건드림.
```cpp
SpinLock mutex;           // ①
LockGuard guard(mutex);   // ② mutex 참조
OrderBook book;           // ③
// 파괴: book → guard(unlock) → mutex
```

### 임시 객체 함정 (`string_view`)
- 표현식 평가 중 생긴 임시 객체는 **full-expression 끝(세미콜론)**까지만 산다.
```cpp
std::string_view sv = build_full_ticker(ex, sym);  // string 임시 → ;에서 파괴
send_to_exchange(sv);                              // 💥 죽은 메모리
const auto& msg = build_full_ticker(ex, sym);      // ✅ 지역 const&/auto&&에 바인딩 → 수명 연장
```
- 수명 연장은 **함수 반환 경계를 넘지 않는다.** 지역 임시를 `const std::string&`로 반환하면 여전히 댕글링.

### 주소 ≠ 소유권
- 포인터/참조 = 가상 주소를 담은 64비트 정수일 뿐. 하드웨어엔 refcount도 GC도 없음.
- 잡는 법: `clang++ -fsanitize=address -fsanitize-address-use-after-scope -g` → ASan이 스코프 탈출 시 스택 바이트를 **poison**, 접근 즉시 크래시 + 스택 트레이스.

### 값으로 반환하라
```cpp
const MarketUpdate& get_latest_quote() { MarketUpdate q = parse_quote(); return q; } // ❌ 스택 참조 반환
MarketUpdate        get_latest_quote() { MarketUpdate q = parse_quote(); return q; } // ✅ NRVO/RVO
```
- C++17 prvalue 반환은 **copy elision 보장**: 호출자가 미리 잡은 공간의 포인터를 넘기고(SysV x86-64는 보통 `%rdi`) callee가 그 자리에 직접 생성 → 복사 0.
- "복사 피하려고 참조/out-param 반환"은 **낡고 위험한 습관.**

- ✅ 체크포인트: 지역 `std::string`을 가리키는 `string_view` 반환 → 해제된 저장소 접근, **UB** (view는 소유 안 함).
- 🎤 "C++이 지역 변수 LIFO 파괴를 보장하는 이유?" → 뒤에 선언된 객체가 앞의 자원에 의존하므로 (lock guard ↔ mutex).
- 🎤 "값 반환이 저지연 시스템 성능에 해롭나?" → 아니다. C++17 필수 copy elision으로 호출자 프레임에 직접 생성.

---

## 2. RAII — 자동으로 일어나는 정리

**핵심:** 자동 객체는 스코프를 떠나는 **모든 경로**(닫는 중괄호, early return, 예외 unwinding)에서 소멸자가 호출된다. RAII = 자원을 객체로 감싸 **획득은 초기화 때, 해제는 소멸자에서 무조건.**

- 관리 대상은 메모리만이 아님: 소켓, epoll fd, `mmap` 공유 버퍼, 락.
- 수동 정리의 문제: early return마다 `close(sock)` 중복 → 누가 새 검사 하나 추가하고 빠뜨리면 **fd 고갈**.

```cpp
class SocketHandle {
    int fd_{-1};
public:
    explicit SocketHandle(int fd) noexcept : fd_(fd) {}
    ~SocketHandle() noexcept { if (fd_ >= 0) close(fd_); }
    SocketHandle(const SocketHandle&) = delete;             // 복사 금지 → double close 방지
    SocketHandle& operator=(const SocketHandle&) = delete;
    SocketHandle(SocketHandle&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }  // 이동 = 소유권 이전
    SocketHandle& operator=(SocketHandle&& o) noexcept {
        if (this != &o) { if (fd_ >= 0) close(fd_); fd_ = o.fd_; o.fd_ = -1; }
        return *this;
    }
    [[nodiscard]] int get() const noexcept { return fd_; }
};
// 사용: 어떤 경로로 나가든 ~SocketHandle()이 정확히 1회
```
**RAII 핸들 체크리스트:** 복사 `delete` · 이동 시 원본 무효화(`-1`/`nullptr`) · 이동 대입은 자기 대입 체크 + 기존 자원 해제 · 전부 `noexcept`.

### 제로 오버헤드
- -O2/-O3에서 단순 소멸자는 epilogue에 **인라인** → `lock_guard`, `SocketHandle` 어셈블리 = 손으로 짠 것과 동일.
- 예외: Itanium ABI는 **테이블 기반 unwinding**(`.eh_frame`), landing pad만 기록 → 정상 경로에 추가 명령 0 ("zero-cost exceptions").
- HFT는 보통 `-fno-exceptions` → 그래도 RAII는 필수: early return / `break` / `continue`에서 `goto cleanup;` 없이 정리.

### Hot path 함정: 소멸자도 코드다
```cpp
void on_tick(const MarketUpdate& t) {
    std::vector<Order> staged;   // 매 틱 힙 할당
    build_signals(t, staged);
    transmit(staged);
}                                // 매 틱 free() ❌
```
→ 컨테이너를 루프 밖/멤버/static으로 올리고 틱마다 `clear()` (capacity 유지).

- ✅ 체크포인트: `lock_guard` 후 early return/예외에도 unlock되는 이유 → 컴파일러가 소멸자 호출을 **epilogue와 예외 landing pad**에 넣음.
- 🎤 "RAII가 수동 관리보다 나은 이유?" → 자원 수명 = 객체 수명. 모든 탈출 경로에서 결정적으로 정리 → 누수·double free 제거.

---

## 3. Raw Pointer · unique_ptr · shared_ptr · weak_ptr

**핵심 질문:** "이 힙 객체의 소멸자를 **누가** 부르나?" 현대 C++은 소유권을 **타입**에 새긴다.
```cpp
void route_order(Order* order);                 // 모호: 누가 delete?
void route_order(std::unique_ptr<Order> order); // 소유권 이전
void route_order(const Order& order);           // 관찰만 (빌림)
```

### `unique_ptr` — 단독 소유, 오버헤드 0
- move-only (복사 생성/대입 삭제). `auto p2 = std::move(p1);` → p1은 `nullptr`.
- 기본 deleter면 **8B** = raw 포인터. 역참조 = 같은 `mov` 한 개.
- **커스텀 deleter 크기:**
  - 함수 포인터 deleter `unique_ptr<FILE, int(*)(FILE*)>` → **16B** (포인터 하나 더 저장)
  - 상태 없는 functor `struct FileCloser { void operator()(FILE*) const noexcept; }` → **8B 유지** (EBO / `[[no_unique_address]]`)

### `shared_ptr` — 공유 소유 + 원자적 refcount
- **16B** = 객체 포인터 + **control block** 포인터. control block(힙) = 원자적 strong/weak count + allocator/deleter.
- 복사 = 원자적 증가(`lock xadd`), 파괴 = acq-rel 원자적 감소.
- 멀티코어에서 by-value로 돌리면 control block 캐시 라인이 **MESI로 코어 간 핑퐁** → 파라미터 전달이 수십 ns 버스 트랜잭션.
```cpp
void process(std::shared_ptr<Order> order);  // ❌ 호출당 원자 연산 2번
void process(const Order& order);            // ✅ 0번
```
- **`make_shared`:** 객체 + control block을 **한 번의 malloc**, 연속 배치 → locality ↑. 단, `weak_ptr`가 남아 있으면 `~T()`는 돌아도 **블록 전체가 해제 안 됨** → 큰 객체면 메모리 고정.
- deleter: `unique_ptr`은 템플릿 타입에 포함(크기 변동), `shared_ptr`은 control block에 type-erase → 항상 16B.

### `weak_ptr` — 비소유 관찰, 순환 끊기
```cpp
struct Session     { std::shared_ptr<Session> partner; };     // 순환 → 영원히 누수
struct SafeSession { std::weak_ptr<SafeSession> partner; };   // strong count가 0이 될 수 있음
```
- 접근은 `wp.lock()` → strong count가 0 아닌지 원자적으로 확인 후 `shared_ptr` 반환.

### 선택 규칙
| 상황 | 타입 |
|---|---|
| 배타적 힙 소유 (기본값) | `unique_ptr` |
| 관찰/수정만 하는 함수 | `const T&` / `T*` (비소유 borrow) |
| 수명이 비결정적인 여러 소유자 | `shared_ptr` (진짜 필요할 때만) |
| 캐시, 순환 끊기 | `weak_ptr` |

- ✅ 체크포인트: 필드만 읽는 검증 함수의 hot-path 시그니처 → **`const Order&`** (할당·원자 연산 없음).
- 🎤 "`unique_ptr`가 raw 포인터 대비 비용 0인 이유?" → 8B, 같은 어셈블리. `std::move`는 레지스터 대입 + 원본 0 쓰기.
- 🎤 "`make_shared` vs `shared_ptr(new T)`?" → 한 번 할당·연속 배치 vs 두 번 할당. 단 weak_ptr 생존 시 블록 전체 유지.

---

## 4. Dangling References — `string_view`, `span`

**핵심:** view는 **값처럼 보이지만 기계적으로는 raw 포인터.** 방어적 힙 복사를 없애주지만, 안전성은 전적으로 **뒤의 저장소 수명**에 달림.

```cpp
void on_itch_message(std::string_view raw) {
    std::string_view stock = raw.substr(11, 8);   // 8바이트 슬라이스, 할당 0
    std::string_view price = raw.substr(19, 10);
    route_tick(stock, price);                     // 소켓 수신 버퍼 안에서 제로카피 파싱
}
```

### 구조와 ABI
- `{ const char* ptr; size_t len; }` = **16B**. SysV x86-64에서 레지스터 2개(`%rdi`, `%rsi`)로 전달.
- **by-value로 넘겨라.** `const std::string_view&`는 스택 포인터를 넘겨 불필요한 역참조 추가.

### 댕글링 3대 패턴
1. **임시 바인딩:** `std::string_view t = get_canonical_ticker("AAPL.O");` → 반환된 `std::string` 임시가 `;`에서 파괴. (`sv = s1 + s2;`도 동일)
2. **컨테이너 재할당:** `span<Order> resting(book.data(), 5);` 후 `book.push_back(x)`가 capacity 초과 → 옛 배열 해제 → span 댕글링. `book` 변수 자체는 멀쩡하지만 **옛 저장소를 가리키던 모든 span/포인터/참조/iterator**가 죽음.
3. **지역 버퍼 view 반환:** `char buf[32]; ... return std::string_view(buf);` → stack-use-after-return.

### 안전 규칙
- view는 **콜 스택 아래로** 파라미터로 넘겨라 (호출자가 호출 동안 수명 보장).
- **비동기 경계를 넘겨 저장하지 마라** → 객체/람다가 현재 프레임보다 오래 살면 `std::string`/`std::vector`로 복사.
- **변경 지점을 넘어 view를 들고 있지 마라** (`push_back`, `insert`, `resize`).

- ✅ 체크포인트: `vector<PriceLevel>`에 대한 span이 `push_back` 후 댕글링되는 조건 → **`size() == capacity()`**였을 때 (재할당으로 원래 버퍼 해제).

---

## 5. Move Semantics Without Magic

**핵심:** "move는 공짜"는 신화. move가 copy보다 싼 건 **훔칠 동적 자원이 있을 때뿐**. 스칼라만 든 flat struct는 move = copy (같은 바이트 복사). 게다가 `noexcept` 없으면 표준 라이브러리가 **move를 거부**한다.

### Value category
- **lvalue:** 식별 가능한 주소·정체성 (이름 있는 변수, 멤버 참조).
- **rvalue:** 임시값, 또는 자원 탈취 허가된 객체 (리터럴, 반환 임시, `std::move` 결과).
- `T&&`는 오버로드 해석에서 rvalue에만 매칭.
```cpp
void submit(const Order& ord);  // lvalue 빌림
void submit(Order&& ord);       // 만료될 rvalue 소비
```

### `std::move`는 명령어 0개
```cpp
template <typename T>
constexpr std::remove_reference_t<T>&& move(T&& t) noexcept {
    return static_cast<std::remove_reference_t<T>&&>(t);
}
```
- lvalue 정체성을 벗겨 **오버로드 해석이 `T&&` 생성자/대입을 고르게** 할 뿐. 실제 일은 move 생성자가 한다.

### move 생성자 해부
```cpp
struct MessageBatch {
    uint64_t sequence_num;
    std::vector<Packet> packets;
    MessageBatch(MessageBatch&& o) noexcept
        : sequence_num(o.sequence_num), packets(std::move(o.packets)) {}
};
```
- `vector` = 포인터 3개(begin, end, cap_end, 24B). move = load 3 + store 3 + 원본에 0 store 3. **힙 버퍼는 제자리.**

### Moved-from 상태 = "valid but unspecified"
- 소멸자는 안전하게 돌아야 함 (null free는 no-op), 재대입 가능해야 함.
- 재대입 전 내용 가정 금지. 표준 컨테이너는 실무상 보통 비어 있지만, 커스텀 타입을 재초기화 없이 읽는 건 논리 오류.

### 왜 `noexcept`가 필수인가
- `vector` 재할당은 **strong exception guarantee** 필요: 중간에 예외 나면 원래 상태로 롤백.
  - copy 중 throw → 새 버퍼만 버리면 옛 버퍼 온전.
  - move 중 throw → 옛 원소 일부가 이미 털림 → 롤백 불가.
- 그래서 `std::move_if_noexcept`:
```cpp
if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>)
    /* 빠른 경로: move로 재배치 */;
else
    /* 느린 폴백: 원소마다 deep copy (할당 폭증) */;
```
- **`noexcept` 빠뜨림 = 재할당마다 조용히 deep copy.** 수백 번의 동적 할당 + 캐시 오염.

- ✅ 체크포인트: 힙 버퍼 관리 클래스의 move 생성자에 `noexcept` 없음 → vector 재할당 시 **copy 생성자로 폴백**. (컴파일러가 `noexcept`를 추론해주지 않음)
- 🎤 "flat struct에서 move가 copy보다 빠르지 않은 이유?" → 훔칠 동적 자원이 없으니 같은 바이트를 복사할 뿐.

---

## 6. Lambdas & Capture Lifetime

**핵심:** 람다 = **`operator()`를 가진 익명 struct.** 캡처 = 그 struct의 **멤버 변수**. 그래서 수명 규칙도 일반 객체와 같다.

```cpp
int sequence_id = 42; double risk_limit = 10000.0;
auto filter = [sequence_id, &risk_limit](const Order& o) {
    return o.seq > sequence_id && o.notional < risk_limit;
};
// 컴파일러 생성물 (개념):
struct __Closure {
    int     sequence_id;   // 값 캡처: 스냅샷 소유
    double& risk_limit;    // 참조 캡처: 내부적으로 포인터, 소유 안 함
    bool operator()(const Order& o) const { ... }
};
```
- 상태 없는 람다 `[]`: `sizeof == 1`, C 함수 포인터로 **암시적 변환** 가능 → C API에 래퍼 없이 전달.

### 비동기 콜백 함정
- `[&]` 참조 캡처는 **같은 스코프 안에서 동기적으로** 실행될 때만 안전 (예: `std::for_each`).
```cpp
void register_subscriber(OrderRouter& router) {
    uint64_t client_id = 9901;
    router.on_fill([&client_id](const Fill& f) { log_fill(client_id, f); }); // 💥
}   // client_id 사라짐 → 콜백은 팝된 스택 프레임을 가리킴
```
- 지연 실행 콜백: **값으로 캡처하거나 소유권을 move**.

### 숨은 `[this]` 캡처
- 멤버 함수에서 `[=]`로 멤버를 쓰면 멤버의 사본이 아니라 **`this` 포인터**를 캡처.
- 객체가 파괴된 뒤 큐의 콜백이 실행 → 댕글링 `this`.
- C++17 `[*this]`: 가볍고 자기완결적인 객체면 **객체 전체를 값 복사**.

### Init capture (C++14) — 소유권을 클로저로
```cpp
auto book = std::make_unique<OrderBook>();
auto task = [b = std::move(book)]() mutable { b->process_quotes(); };  // 클로저가 unique_ptr 소유
```

### Hot-path 비용
- 템플릿 인자로 넘기면 `operator()` 완전 인라인, 오버헤드 0.
- `std::function`에 담으면 SBO(보통 16–24B ≈ 포인터 3개) 초과 캡처 시 **숨은 힙 할당**. 예: 포인터 1개 캡처(8B) OK / double 4개(32B) → malloc.

- ✅ 체크포인트: 네트워크 스레드가 지역 변수를 `[&]` 캡처한 람다를 큐잉 → **바깥 함수 프레임이 반환된 후 실행될 때마다** UB.
- 🎤 "람다가 클래스 멤버를 캡처하면 내부적으로?" → `this`를 암시적으로 캡처. 인스턴스가 먼저 파괴되면 크래시.

---

## 7. Container Invalidation & Stable Handles

**핵심:** 무엇이 포인터를 깨는지는 컨테이너의 **바이트 배치**가 결정한다. 이 버그는 **획득한 곳이 아니라 한참 뒤 다른 핸들러**에서 터진다 (콜백 A가 포인터 저장 → 이벤트 B가 push_back → 핸들러 C가 cancel → 페이지 폴트, 최악은 재활용된 메모리를 덮어씀).

### 컨테이너별 무효화 규칙
| 컨테이너 | 삽입 | 삭제 | 포인터/참조 | iterator |
|---|---|---|---|---|
| `vector` (capacity 초과) | 새 블록(1.5x/2x) → 이동 → 옛 블록 해제 | — | **전부** 무효 | **전부** 무효 |
| `vector` (capacity 내 중간 삽입) | 삽입 위치부터 뒤로 shift | 삭제 위치부터 앞으로 shift | 해당 위치 ~ `end()` 무효 | 동일 |
| `unordered_map` rehash | bucket 배열만 재할당, 노드는 제자리 (next 포인터 재배선) | 해당 노드만 | ✅ **유효** | ❌ 무효 (bucket 배열을 가리킴) |
| `list`/`map`/`set` (노드 기반) | 영향 없음 | 삭제된 노드만 | ✅ 유효 | ✅ 유효 |

- 노드 기반은 안정적이지만 **노드마다 개별 할당 → 순회 시 L1/L2 미스** → hot path엔 부적합.
- `const Order* p = &order_map[id];`는 rehash를 몇 번 겪어도 살아남는다.

### 프로덕션의 안정 핸들
1. **시작 시 크기 확정:** 세션 최대 10만 주문이면 `orders.reserve(100'000)` → 하루 종일 `size < capacity` → append가 포인터를 절대 안 깸 + hot-path 할당 제거.
2. **정수 인덱스 (`uint32_t`):** 포인터 = 물리 가상 주소, 인덱스 = 상대 오프셋. 버퍼가 `0x7f0010` → `0x7f0080`으로 옮겨도 `orders[4]`는 여전히 4번.
3. **슬롯 재사용 시 ABA 문제:** 4번 주문 취소 → 새 호가가 4번 슬롯 차지 → 옛 인덱스 4를 들고 있던 서브시스템이 **새 주문을 취소**.
4. **Generational slot handle:**
```cpp
struct OrderHandle { uint32_t index; uint32_t generation; };
struct Slot        { Order order; uint32_t generation; bool occupied; };

Order* get(OrderHandle h) {
    if (h.index < slots.size()
        && slots[h.index].generation == h.generation
        && slots[h.index].occupied)
        return &slots[h.index].order;
    return nullptr;   // stale: 이미 체결/취소됨
}
// 슬롯 해제·재활용 시 generation++
```
→ 평탄 배열 O(1) 인덱싱 + 재배치 면역 + stale 즉시 감지, 포인터 체이싱·refcount 없음.

- ✅ 체크포인트: `unordered_map` rehash → **iterator는 전부 무효, 참조·포인터는 유효.**
- 🎤 "저지연 시스템은 무효화 버그를 어떻게 막나?" → capacity 선예약, 주소 대신 정수 오프셋, (index, generation) 핸들로 O(1) stale 감지.

---

## 8. C++ Casts & Type Safety

**핵심:** C 스타일 `(T)x`는 상황에 따라 const/static/reinterpret 캐스트(또는 조합)로 **경고 없이** 변신한다. 명명 캐스트 4종은 각각 다른 주장·코드 생성·런타임 비용을 가진다.

| 캐스트 | 하는 일 | 런타임 비용 | 위험 |
|---|---|---|---|
| `static_cast` | 컴파일 타임에 관계를 아는 변환: 수치(`cvtsi2sd` 등), `void*`→`T*`, 계층 이동(고정 오프셋 조정) | 0 (수치 변환 명령 제외) | **downcast는 검증 없음** → 실제 타입 아니면 UB |
| `dynamic_cast` | RTTI로 안전한 downcast: vptr → vtable → `type_info`, `__dynamic_cast`가 상속 DAG 순회 | **있음** (메타데이터 캐시 라인 여러 개 + 간접 분기) | 실패 시 포인터는 `nullptr`, 참조는 `std::bad_cast` |
| `reinterpret_cast` | 비트를 다른 타입으로 취급 | **명령 0개** | 정렬 위반, strict aliasing |
| `const_cast` | const/volatile 한정자만 조작 | **명령 0개** | 원래 `const`로 선언된 객체에 쓰기 = UB |

### `reinterpret_cast` — 제로카피 와이어 파싱
```cpp
alignas(WirePacket) uint8_t rx_buffer[1024];
// ... 소켓 수신 ...
const WirePacket* pkt = reinterpret_cast<const WirePacket*>(rx_buffer);
process_packet(pkt->sequence, pkt->price);
```
- **정렬:** `WirePacket`이 8B 정렬 요구(`uint64_t` 포함)인데 버퍼가 비정렬이면 → 비정렬 접근 페널티, ARM에선 **SIGBUS**.
- **Strict aliasing:** 서로 다른 타입 포인터가 같은 메모리를 가리키지 않는다고 컴파일러가 가정 (예외: `char*`, `std::byte*`). `int*`/`float*`로 같은 곳에 쓰면 최적화가 로드를 재배치·제거 → 데이터 손상.
- 프로덕션 대응: `-fno-strict-aliasing` 또는 **`std::memcpy`** (현대 컴파일러는 레지스터 로드로 최적화).

### `const_cast`
- 정당한 용도: `char*`를 받지만 수정 안 하는 레거시 C API.
- const 제거 자체는 정의됨. **진짜 const 객체에 쓰면 UB** — const 전역은 `.rodata`(읽기 전용 페이지)에 있어 쓰기 시 즉시 폴트.

- ✅ 체크포인트: hot loop에서 `Strategy*`의 구체 타입을 매번 `dynamic_cast` 체인으로 판별 → **virtual 함수나 `std::variant`로** 런타임 타입 검사 없이 디스패치.
- 🎤 "`reinterpret_cast`로 패킷 파싱이 위험한 이유?" → 생성자 우회 + 정렬 트랩 + strict aliasing UB.
- 🎤 "C 스타일보다 명명 캐스트를 쓰는 이유?" → 리팩터링으로 타입이 바뀌면 C 스타일은 조용히 reinterpret/const로 격하 → 명명 캐스트는 컴파일 에러로 잡아줌.

---

## 용어 사전

| 용어 | 한 줄 정의 |
|---|---|
| Automatic storage | 스코프에 묶인 스택 객체, 역순 파괴 |
| Full-expression | 임시 객체 수명의 경계 (보통 세미콜론) |
| Lifetime extension | 지역 `const&`/`auto&&`에 임시를 바인딩하면 참조 스코프까지 연장 (반환 경계 X) |
| Guaranteed copy elision | C++17 prvalue 반환 시 호출자 공간에 직접 생성 |
| ASan use-after-scope | 스코프 이탈 스택 바이트 poison → 접근 즉시 검출 |
| RAII | 획득=초기화, 해제=소멸자 |
| Zero-cost exceptions | 테이블 기반 unwinding, 정상 경로 명령 추가 0 |
| Control block | `shared_ptr`의 힙 메타데이터 (strong/weak 원자 카운트, deleter) |
| EBO / `[[no_unique_address]]` | 빈 타입 멤버가 공간을 차지하지 않게 하는 최적화 |
| View | 비소유 {ptr, len} (`string_view`, `span`) |
| Value category | lvalue / xvalue / prvalue — 오버로드 선택을 결정 |
| `move_if_noexcept` | nothrow move일 때만 move, 아니면 copy |
| Closure | 람다가 만드는 익명 struct 객체 |
| Init capture | `[b = std::move(x)]` 형태의 일반화 캡처 |
| ABA problem | 재사용된 슬롯/주소를 옛 핸들이 새 객체로 오인 |
| Generational handle | (index, generation)으로 stale 핸들 감지 |
| RTTI | 런타임 타입 정보, `dynamic_cast`/`typeid`가 사용 |
| Strict aliasing | 다른 타입 포인터는 같은 메모리를 가리키지 않는다는 가정 |
