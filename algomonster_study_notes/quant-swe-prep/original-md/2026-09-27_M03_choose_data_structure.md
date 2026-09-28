# Module 3 — Choose The Right Data Structure (올바른 자료구조 고르기)

> 출처: AlgoMonster Quant SWE Interview Prep, Module 3 (레슨 7개) · 한국어 개념 정리 노트
> 목표: "여기엔 어떤 컨테이너를 쓰고 왜?"에 **Big-O가 아니라 접근 패턴·변경 패턴·메모리 동작**으로 답하기. 컨테이너 내부(성장, bucket, 노드, chunk), 할당 출처(pmr/pool), SBO, 템플릿까지.

## 한눈에 보기 (5분 복습용)

| 컨테이너 | 내부 구조 | 강점 | 대가 | 무효화 |
|---|---|---|---|---|
| `vector` | 연속 버퍼 1개 (ptr/size/cap) | 스캔 최강, 인덱스 O(1) | 성장 시 재할당·이동, 중간 삽입 shift | 성장 시 전부 |
| `unordered_map` | bucket 포인터 배열 + **노드별 할당** 체인 | 키 조회 평균 O(1) | 노드 포인터 체이싱, rehash O(n) 스파이크 | rehash 시 iterator만 |
| `map`/`set` | **red-black tree**, 노드당 포인터 3개 + 색 비트 | 정렬 순회, 안정 핸들 | O(log n), 노드당 할당, 포인터 체이싱 | 지운 원소만 |
| `list` | 이중 연결 노드 | 위치 알면 O(1) insert/erase/**splice** | 랜덤 접근 없음, **STL 최악 locality** | 지운 원소만 |
| `deque` | 고정 크기 chunk들 + chunk 포인터 배열 | 양끝 O(1) push, 인덱스 O(1)(간접 2번) | chunk 사이 불연속 | 양끝 push: 참조 유지, iterator 무효 |

**기본값은 `vector`.** 벗어날 이유(키 조회 / 중간 삽입 안정 핸들 / 양끝 성장 / 정렬 순회)가 있을 때만 흩어진 메모리 비용을 지불.

**할당 출처 바꾸기:** 한 번에 같이 죽는 것 → **arena** (`monotonic_buffer_resource`), 개별로 오가는 것 → **pool** / 오브젝트 풀. hot path에서 전역 힙이 **절대 안 나오게**.

---

## 1. STL Container Choice

**상황:** AAPL 라이브 주문 보관. ① 취소 요청이 order id로 주문을 찾아야 함 ② 최우선 가격부터 순회해 호가 게시. **한 컨테이너로 둘 다 최선일 수 없다.**

- Big-O는 가장 쓸모없는 축 — 성숙한 컨테이너는 다 기대한 복잡도를 낸다. 레이턴시 차이는 그 **아래**(메모리 배치, 어떤 연산이 메모리를 옮기나)에 있다.

### 컨테이너를 고르는 세 질문
1. **접근 패턴:** 위치로(→ `vector`/`deque` 인덱스)? 키로(→ map)? 전체 순회(→ 연속 컨테이너가 훨씬 빠름)? 정렬 순회(→ ordered 컨테이너만 공짜로 유지)?
2. **변경 패턴:** 어디에 삽입/삭제? 끝에 append는 vector/deque 쌈. 중간 삽입은 노드 기반(`list`)만 쌈. 변경 후에도 **핸들이 살아야 하나?** 그렇다면 안정성은 필수 요건.
3. **메모리 동작 (면접관이 실제로 파는 질문):** 연속 → 캐시 라인 스트리밍 + prefetch. 노드 기반 → 매 스텝 포인터 체이싱. deque → 중간.

### "가장 빠른 map은 사실 vector"
- 키가 **작고 조밀한 정수** → `data[key]` 배열 인덱싱. 해싱·bucket·노드 없음.
- 작고, 한 번 만들고 여러 번 검색 → **정렬된 vector + 이진 탐색**이 트리보다 빠름 (연속 메모리 스트리밍 vs 포인터 체이싱).

```cpp
// 같은 AAPL 주문, 일이 다르면 컨테이너도 다르다
std::unordered_map<std::uint64_t, Order> by_id;                // Job1: id로 취소 → 해시
Order& o = by_id.at(cancel.order_id);                          // 평균 O(1), 노드는 흩어짐
std::map<std::int64_t, Order, std::greater<>> by_price;        // Job2: 최우선가부터 → 정렬 map
for (auto& [price, order] : by_price) { /* ... */ }            // 정렬 순회, 노드마다 체이싱
std::vector<Trade> tape; tape.push_back(t);                    // Job3: 체결 로그 → vector
```
- ✅ 체크포인트: 시작 시 64개 종목 레코드, 작은 정수 id로 hot path에서 수백만 번 조회 → **id로 직접 인덱싱하는 `vector`/배열**.
- 🎤 "STL 컨테이너 어떻게 고르나?" → 접근 패턴·변경 패턴·메모리 동작. Big-O는 다 비슷, 차이는 레이아웃과 메모리를 옮기는 연산.
- 🎤 "기본 컨테이너와 벗어나는 경우?" → `vector`. 키 조회, 중간 삽입 안정 핸들, 양끝 성장, 정렬 순회가 강제할 때만.

---

## 2. vector — size, capacity, growth, reserve

**핵심:** vector는 길이 두 개를 추적한다. **size** = 저장된 원소 수, **capacity** = 현재 버퍼가 성장 없이 담을 수 있는 수. 그 차이가 "다음 몇 번의 push_back은 할당 없음"이라는 여유분.

```cpp
std::vector<int> v;   // size 0, cap 0
v.push_back(1);       // 1, 1  ← 첫 할당
v.push_back(2);       // 2, 2  ← 성장
v.push_back(3);       // 3, 4  ← 성장, 여유 1
v.push_back(4);       // 4, 4  ← 여유 사용, 성장 없음
```

### 왜 더하지 않고 곱하나 (= amortized O(1)의 증명)
- 성장 배수: libstdc++/libc++ **2x**, MSVC **1.5x** (구현 정의 → 면접에선 "상수 배"라고).
- N까지 채울 때 성장은 cap 1,2,4,8,… → **약 log₂N번**. 재배치 총량 1+2+4+…≈ **N**. N번 push로 나누면 push당 상수.
- 고정 10칸씩 늘리면: N/10 + 2N/10 + … ≈ **N²/20** → push당 O(n). **배수가 전부다.**
- 대부분 push는 공짜, 몇 번은 O(n), 평균은 O(1) = amortized.

### 성장은 move일까 copy일까
- 재배치 시 move를 원하지만 move는 **원본을 파괴적으로 변경**. 중간에 throw하면 반쯤 털린 옛 버퍼를 복구 불가 → **strong exception guarantee**(실패한 push_back은 vector를 그대로 둠) 위반.
- 그래서 move 생성자가 **`noexcept`일 때만 move**, 아니면 **copy**. 힙 소유 타입이면 성장마다 전 원소 deep copy = 피하려던 할당 그대로.
- **해결: 키워드 하나.** move 생성자에 `noexcept`.

### reserve / resize / shrink_to_fit
| 호출 | capacity | size | 용도 |
|---|---|---|---|
| `reserve(n)` | ≥ n으로 한 번 할당 | 변화 없음 | 알려진 개수를 push할 때. **성장 제거 + 핸들 안정** |
| `resize(n)` | 필요 시 증가 | n으로 (원소 생성/파괴) | 원소가 **지금** 존재해야 할 때 |
| `shrink_to_fit()` | size 쪽으로 줄이기 **요청**(무시 가능) | 변화 없음 | 보통 작은 버퍼 할당 + 복사 → **cold path 전용** |
- 🐛 실수: `reserve(1000)` 후 `v[0] = x` → **UB** (0번 원소가 아직 존재하지 않음).
- 🎤 "push_back이 O(1)이 아니라 amortized O(1)인 이유?" → 상수 배 성장 → 재할당 ~log n번, 전체 재배치 ~n개. n번에 나누면 상수. 단, 성장을 유발한 그 한 번은 O(n).
- 🎤 "reserve가 바꾸는 것과 안 바꾸는 것?" → capacity를 올려 버퍼를 한 번 할당. size·원소 생성은 안 함. 루프에서 성장 제거 + 핸들 안정.

---

## 3. unordered_map — bucket, load factor, rehash

**핵심:** 평균 O(1)은 **사실이지만 오해를 부른다.** 그 아래에 bucket, 충돌 체인, 주기적 O(n) rehash가 있고, 저지연 시스템에선 이게 **꼬리(tail)**에 나타난다.

### 해시 → bucket 경로
```cpp
std::size_t h = hash(order_id);           // 키 → 큰 정수
std::size_t idx = h % buckets.size();     // 정수 → bucket 슬롯
Node* n = buckets[idx];                   // bucket으로 점프
while (n && n->key != order_id) n = n->next;  // 체인 순회
```
- 충돌은 정상. `std::unordered_map`은 **separate chaining** (bucket마다 연결 리스트).
- **구조적 핵심: 노드 기반.** 키/값 쌍마다 개별 할당 노드, bucket 배열은 포인터만. 원소 하나짜리 bucket도 **캐시에 없을 법한 노드로 포인터 체이싱**.

### Load factor와 rehash 스파이크
- **load factor** = `size / bucket_count` (bucket당 평균 원소 수). 기본 `max_load_factor` = **1.0**.
- 삽입이 한도를 넘기면 **rehash**: bucket 수 약 2배 → 모든 원소 재삽입 (`hash % bucket_count`가 바뀌므로). 그 한 번의 삽입이 **O(n)**. vector 성장과 같은 긴 꼬리 모양.
- 무효화 (Module 2 연결): 노드는 안 움직이므로 **포인터·참조 유효, iterator만 무효**.

### 평균 O(1)이 숨기는 세 가지
1. **나쁜 해시** → 한 bucket에 몰림 → 체인이 선형 스캔 → 최악 O(n).
2. **rehash** → 평균엔 안 보이고 **p99.9에서 보임**.
3. **노드 레이아웃** → 모든 조회가 흩어진 노드로 체이싱, Big-O가 세지 않는 캐시 미스.
- 1차 처방: `reserve(n)`으로 bucket 배열을 미리 → hot path rehash 제거.

### 체이싱 자체가 문제라면: flat / open-addressing map
- bucket 포인터 대신 **엔트리를 하나의 연속 배열에 직접 저장**, 충돌은 인접 슬롯 **probing**. 몇 개 캐시 라인 안에서 조회, 원소별 할당 없음.
- 예: `absl::flat_hash_map`, robin-hood 계열.
- 대가: erase·높은 load factor가 까다롭고, 충돌 클러스터가 더 아프게 때림. 읽기 위주 hot path면 locality가 보통 이김.
- **면접 포인트:** 특수 map이 이기는 이유는 **Big-O가 아니라 레이아웃**.

- ✅ 체크포인트: 조회 경로 캐시 미스 + 천 번 중 한 번 삽입 stall → 미스 = **노드 포인터 체이싱**, stall = **O(n) rehash**. `reserve`는 **rehash만** 고침.
- 🎤 "rehash가 뭐고 언제?" → 삽입이 load factor를 `max_load_factor` 넘게 만들면 더 큰 bucket 배열 할당 + 전 원소 재삽입. O(n) 스파이크, `reserve`로 제거.

---

## 4. map, list, deque 트레이드오프

**핵심:** 정렬·안정성·양끝 성장은 진짜 강점이지만, hot path에서의 대가는 보통 **locality**(+ 메모리).

### `std::map` = 균형 BST (실무상 red-black tree)
- 노드 = 키 + 값 + **left/right/parent 포인터 + 색 비트**.
- 조회/삽입/삭제 = 루트→리프 경로 길이 ~log₂n, 매 단계 키 비교 → **O(log n)**.
- n개 = n번 할당, 노드 흩어짐, 조회·중위 순회 매 스텝 체이싱.
- 얻는 것: **정렬 순회 공짜 + 안정 핸들**(그 원소를 지우기 전까지 유효).

### `std::list` = 이중 연결 리스트
- 유일한 진짜 강점: iterator를 들고 있으면 그 위치 insert/erase **O(1), 아무것도 안 움직임**. 다른 list로 **splice O(1)**. 노드 불변 → 핸들 안정.
- 대가: 랜덤 접근 없음(i번째 = i개 노드 걷기), 노드별 할당, **STL 최악 locality**.
- "중간 삽입이니까 list"조차 **vector가 자주 이긴다** — 연속 메모리 shift가 포인터 체이싱보다 훨씬 빨라서, 서류상 일을 더 해도 이김.
- 🎤 "list를 실제로 언제 쓰나?" → **"거의 안 쓴다."** 들고 있는 위치에서 O(1) splice + splice 전후 안정 핸들이 **둘 다** 중요할 때만. 반사적으로 list를 꺼내지 않는 게 면접관이 원하는 신호.

### `std::deque` = chunk들의 나열
- 여러 개의 고정 크기 chunk + chunk 포인터 작은 배열.
- `push_back`/`push_front` **O(1)** (vector는 앞쪽이 비쌈), 인덱스 접근 O(1) (간접 2번: chunk 찾기 → 오프셋).
- chunk 내부 연속, chunk 간 흩어짐 → 순회는 list보다 훨씬 낫고 vector보다 약간 못함.
- 무효화: 양끝 push는 **포인터·참조 유지**(기존 chunk 안 움직임), **iterator는 무효**.
- → **큐**에 자연스러움 (한쪽에서 넣고 다른 쪽에서 소비, 대기 항목 참조가 새 도착에도 살아남음).

### 공통 트레이드: 안정성·정렬 = locality + 메모리 비용
- 노드 오버헤드: list 노드 = 원소 + 포인터 2개, map 노드 = 포인터 3개 + 색 비트 → **작은 값이면 오버헤드가 페이로드를 압도**.
- 같은 long 1000개: vector는 **할당 1번**, list/map은 **원소당 1번**, 바이트 총량도 페이로드를 훨씬 초과.

- ✅ 체크포인트: 수천 개 대기 주문을 매 업데이트마다 처음부터 끝까지 스캔, 중간 삽입 거의 없음, 동료가 "핸들 안정 위해 list" → 틀린 이유: **매 스텝 개별 노드 체이싱으로 locality 상실, 워크로드는 splice가 아니라 스캔 위주.**
- 🎤 "deque가 큐에 좋은 이유?" → 양끝 O(1) push, 기존 원소 참조 유지(chunk 불이동), chunk 내 locality.

---

## 5. pmr, Allocators, Object Pools

**원칙 (Module 1):** 정상 상태에선 할당 0. 모든 컨테이너는 기본적으로 전역 힙(free-list 탐색, 가끔 락, 긴 꼬리)을 쓴다 → **메모리 출처를 바꾼다.**

### 컨테이너는 allocator에서 메모리를 얻는다
- 숨은 템플릿 파라미터: `std::vector<T>` = `std::vector<T, std::allocator<T>>` (기본은 전역 new/delete로 전달).
- allocator를 바꾸면 모든 원소 버퍼·노드·rehash의 출처가 바뀜 — **사용 코드 수정 없이**.
- 고전 allocator의 불편: 템플릿 파라미터라 allocator가 다르면 **다른 타입** → 서로 대입 불가.

### pmr (C++17) — 메모리 출처를 런타임에
- `std::pmr::vector`, `std::pmr::list` 등은 **memory resource 포인터**(allocate/deallocate 가진 객체)를 든 allocator 하나를 공유.
- 리소스를 넘겨 런타임에 출처 선택. 출처가 달라도 `std::pmr::vector<int>`는 **같은 타입**.
- 비용: 할당당 virtual 호출 1회 — 그것이 조종하는 할당에 비하면 무시 가능 (게다가 hot path에선 할당 자체를 없애는 중).
- `new_delete_resource()` = 전역 힙(기본). 레이턴시용 핵심 두 개 = **arena**, **pool**.

### Arena: `std::pmr::monotonic_buffer_resource`
- 미리 크기 잡은 블록 하나 → 할당 = **현재 위치 반환 + 포인터 전진** (free-list 탐색·크기 bucket·락 없음).
- 개별 해제 = **아무것도 안 함**. 리소스 파괴/리셋 시 포인터를 처음으로 → **O(1) 전체 회수**.
- **이벤트별 스크래치 메모리**에 딱: 마켓 업데이트마다 arena → 핸들러가 자유롭게 할당(임시 vector, 노드, 파싱된 메시지) → 업데이트 끝에 리셋.

### Pool: `std::pmr::unsynchronized_pool_resource` / 손으로 짠 object pool
- 개별로 생성·소멸하는 객체용 (새 주문에 `Order` 생성, 체결 시 파괴).
- 고정 크기 chunk free list — 시스템 allocator와 같은 아이디어지만 **나만 씀, 스레드 공유 없음, 남의 락 없음**.
- **Object pool (트레이딩 코드에서 가장 흔한 형태):** 시작 시 `Order` 고정 배열 + 미사용 free list. 생성 = pop, 해제 = push. `new`/`delete`/allocator 호출 0, 포인터 몇 번 이동, 객체들이 **붙어 있어 locality**까지.
```cpp
// 개념 스케치 (노트용)
struct OrderPool {
    std::vector<Order>  slots;   // 시작 시 한 번 할당
    std::vector<Order*> free;    // 미사용 슬롯
    Order* acquire()        { Order* o = free.back(); free.pop_back(); return o; }
    void   release(Order* o){ free.push_back(o); }   // free도 미리 reserve
};
```

### 수명으로 결정
| 수명 패턴 | 선택 |
|---|---|
| 한 배치가 같이 살고 같이 죽음 | **arena**, 경계에서 리셋 |
| 객체가 개별적으로 오감 | **pool** / 시작 시 크기 잡은 object pool |
→ 편한 컨테이너는 유지하고, **할당 결정을 레이턴시 무관한 시작 시점으로** 옮기는 것.

- ✅ 체크포인트: 업데이트마다 임시 컨테이너 몇 개 할당, 업데이트 끝에 전부 해제 → **`monotonic_buffer_resource`** (포인터 bump + 끝에 리셋 한 번 O(1)).
- 🎤 "`std::vector`를 전역 힙에서 떼어내려면?" → 다른 allocator. pmr이면 런타임에 memory resource 전달: 배치 수명 임시는 arena, 개별 재활용은 pool. 컨테이너 코드는 그대로.

---

## 6. Small Buffer Optimization (SBO)

**핵심:** 객체가 **자기 안에 작은 고정 버퍼**를 품고, 내용이 작으면 거기 인라인 저장(할당 0), 넘치면 힙 버퍼 할당 + 포인터. 한 객체, 두 레이아웃 + 모드 플래그.

### SSO (small-string optimization)
- `std::string` = **24–32B 객체** (라이브러리마다 다름) 안에 작은 문자 버퍼.
- 인라인 용량: libstdc++ ~**15자**, libc++ ~**22자** (구현 정의 → 면접에선 범위로).
- `std::string sym = "AAPL";` → **할당 0**. `sizeof(std::string)`이 포인터보다 큰 이유 = 대부분 인라인 버퍼.

### 트레이드오프
- 인라인 공간은 공짜가 아님 → **모든 인스턴스가 커짐** (써도 안 써도). 문자열 배열은 원소마다 버퍼를 달고 다니고, 긴 문자열에선 완전 낭비.
- 대신 흔한 작은 경우: 할당 0, 포인터 체이싱 0, 문자가 size 옆에 **이미 캐시에**.
- "대부분의 인스턴스는 작다"에 거는 베팅 — 티커·짧은 심볼·키에선 보통 이김.

### 문자열 너머
- `std::function`: 작은 callable 인라인, 큰 캡처는 힙 (Module 1의 그 비용).
- small vector: `boost::small_vector`, `llvm::SmallVector` — 처음 몇 원소 인라인, 초과 시에만 할당.

### Hot path 읽기
- 티커 `std::string`은 사실상 공짜, 짧은 동안 복사도 힙 밖.
- **임계치를 넘는 순간 모든 복사가 다시 할당** → 긴 로그 라인, 이어붙인 메시지는 힙 비용.
- ✅ 체크포인트: `sizeof(std::string)`이 8이 아니라 24–32인 이유 → **인라인 문자 버퍼** (짧은 문자열 할당 0).

---

## 7. Templates & Generic Programming

**핵심:** 템플릿은 코드가 아니라 **레시피**. `std::vector<int>`를 쓰는 순간 컴파일러가 빈칸을 채워 구체 클래스를 찍어냄 = **instantiation**. 타입마다 구체 버전 생성 = **monomorphization**.

- `vector<int>`와 `vector<Order>`는 **진짜 다른 타입, 다른 기계어**. 소스는 generic, 바이너리는 아님.
- 타입이 컴파일 타임에 고정 → 호출 대상을 알고 **인라인·교차 최적화** (vtable·함수 포인터·런타임 선택 없음) = **zero-cost abstraction**. `std::sort` on `vector<int>`는 정수 비교가 루프에 인라인.

### Template vs Virtual (면접 단골)
| | Template | Virtual |
|---|---|---|
| 코드 | 타입마다 한 벌 (여러 개) | 한 벌 |
| 디스패치 | 컴파일 타임, 인라인 가능 | 런타임 vtable, 인라인 불가 |
| 트레이드 | **코드 크기 ↔ 속도** | **속도 ↔ 단일 코드 경로** |

### 대가
- **코드 비대화:** 10개 타입으로 쓴 vector = 10벌 → 바이너리 크기 + **I-cache 압박** (hot path에선 데이터 locality만큼 중요할 수 있음).
- 느린 컴파일, 긴 에러 메시지 (인스턴스화될 때에야 본문 검사). → 모든 걸 무작정 템플릿화하지 않음.

### Template vs type erasure (다시)
- callable을 **템플릿 파라미터**(`F&&`, `std::sort`의 모양)로 → 구체 타입 유지 → 비교가 sort 안에 인라인.
- **`std::function`**으로 → 타입 소거 → 매 비교가 인라인 불가 간접 호출. 같은 알고리즘, 측정 가능한 속도 차.
- ✅ 체크포인트: `vector<int>`가 generic인데 빠른 이유와 주 비용 → `int`로 인스턴스화된 별도 구체 버전이 완전 최적화, 비용은 **코드 비대화**.
- 🎤 "템플릿의 단점?" → 인스턴스화마다 바이너리에 별도 사본 → 크기·I-cache 압박, 느린 컴파일, 거친 에러 메시지.

---

## 용어 사전

| 용어 | 한 줄 정의 |
|---|---|
| Access / mutation pattern | 원소에 도달하는 방식 / 삽입·삭제 위치와 핸들 생존 요구 |
| Capacity | 성장 없이 담을 수 있는 원소 수 |
| Geometric growth | 상수 배(2x/1.5x) 성장 → amortized O(1) |
| Strong exception guarantee | 실패한 연산은 상태를 원래대로 둠 |
| Separate chaining | bucket마다 연결 리스트로 충돌 해결 |
| Load factor | size / bucket_count (기본 max 1.0) |
| Rehash | bucket 배열 확장 + 전 원소 재삽입 (O(n)) |
| Open addressing / flat map | 연속 배열에 엔트리 직접 저장, probing으로 충돌 해결 |
| Red-black tree | `std::map`/`set`의 균형 BST |
| Splice | list 노드를 옮겨 붙이기, O(1) |
| Allocator / memory resource | 컨테이너가 메모리를 얻는 곳 / pmr의 런타임 출처 객체 |
| Monotonic buffer (arena) | bump-pointer 할당, 개별 해제 없음, 일괄 리셋 |
| Object pool | 시작 시 고정 객체 배열 + free list |
| SBO / SSO | 객체 내부 인라인 버퍼로 작은 경우 할당 회피 |
| Monomorphization | 템플릿을 타입마다 구체 코드로 찍어내기 |
| Zero-cost abstraction | 추상화 비용이 컴파일 타임에 해소되어 런타임 0 |
