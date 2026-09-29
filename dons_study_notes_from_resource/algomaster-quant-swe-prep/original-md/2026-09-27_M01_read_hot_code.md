# Module 1 — Read Hot Code (핫 코드 읽기)

> 출처: AlgoMonster Quant SWE Interview Prep, Module 1 (레슨 12개) · 한국어 개념 정리 노트
> 목표: "이 O(n) 루프가 왜 느린가?"를 **C++ 소스 → 메모리 레이아웃 → CPU → 할당 → 디스패치 → 측정**까지 한 줄로 설명할 수 있게 되기.

## 한눈에 보기 (5분 복습용)

| 체크 포인트 | 숨은 비용 | 처방 |
|---|---|---|
| 데이터 레이아웃 | 포인터 체이싱, 캐시 미스 (DRAM ~50–70ns) | `vector<T>` 연속 저장, hot 필드 묶기 / SoA |
| 할당 | `push_back` 재할당, `unordered_map` 노드·rehash, `std::function` 힙 | `reserve()`, `clear()` 후 재사용, pool/arena |
| 복사 | by-value 파라미터, `for (Order o : v)` 딥카피 | `const&`, 소유권 이전은 `std::move` |
| 분기 | 예측 실패 시 파이프라인 flush 15–20 cycle | branchless(`cmov`), 정렬/파티셔닝 |
| 간접 호출 | virtual / 함수 포인터 / `std::function` → **인라이닝 불가** | template, `std::variant`+`visit`, 배치 단위 virtual |
| 측정 | DCE, 상수 폴딩, cold cache, 평균의 함정 | `DoNotOptimize`, 워밍업, 코어 고정, p99/p99.9 |

**면접 공식:** "둘 다 O(n)이니 **한 iteration의 실제 일**을 비교하겠다 → 무엇을 읽나 / 어디에 있나 / 할당하나 / 복사하나 / 예측 가능한가 / 기다리나 / 제대로 측정했나."

---

## 0. 코스 개요 (Course Overview)

- 퀀트/HFT SWE 면접 = LeetCode + **코드가 실제 기계에서 어떻게 도는가** (메모리, 캐시, 할당, 스레드, 네트워크, 레이턴시).
- 대상: LeetCode는 되는데 시스템 레이어(C++, 성능, 동시성, 네트워킹, 트레이딩 용어)가 비어 있는 사람.
- 금융 용어는 구체 예시로: `AAPL` 티커, 마켓 업데이트("AAPL 500주 $190.10 매도 의향"), 주문("AAPL 100주 $190.10 매수").
- **Hot path** = 업데이트·주문이 흐르는 동안 계속 반복 실행되는 코드 경로.
- 레슨 구조: 면접 질문 → 멘탈 모델 → C++ 형태 → 소리 내어 말할 답변. 같은 질문이 뒤 모듈에서 더 깊게 재등장 (예: vector vs list → 컨테이너 → 캐시 → 벤치마킹).

---

## 1. The Cost Model — Big-O가 숨기는 것

**핵심:** Big-O는 *반복 횟수의 증가율*만 말한다. 같은 O(n)이라도 **iteration 1회에 기계가 하는 일**이 다르면 몇 배 차이 난다.

- 한 iteration 안에 숨을 수 있는 것: 흩어진 포인터 따라가기, 힙 할당, 큰 객체 복사, 간접 호출, OS 대기.
- **Cost model** = 반복되는 연산을 추적해서 어떤 게 iteration을 지배하는지 묻는 것.

### 9가지 Hot-path 질문 (순서대로 공격)
1. 무엇이 반복 실행되나?
2. 각 iteration이 **어떤 데이터**를 읽나?
3. 그 데이터는 메모리 **어디에** 있나? → (2·3: locality 레슨)
4. 루프 안에서 어떤 함수 호출이 일어나나? → (dispatch)
5. 그 호출이 **메모리를 할당**할 수 있나? → (allocation)
6. 보이는 것보다 **더 많은 바이트를 복사**하나? → (copy/move)
7. CPU/컴파일러가 **경로를 예측**할 수 있나? → (branch)
8. OS·락·네트워크·로그를 **기다리나**? → (Module 6–7)
9. **진짜 hot path를 측정**하고 있나? → (benchmarking)

### 기준 코드 (모듈 내내 재사용)
```cpp
struct MarketUpdate {
    std::uint64_t sequence;
    char          symbol[8];  // "AAPL"
    std::int64_t  price;      // 19010 = $190.10 (정수 틱)
    std::int32_t  qty;        // 100
};

void handle(const std::vector<MarketUpdate>& updates) {
    for (const MarketUpdate& update : updates) {
        if (update.symbol[0] == 'A') {   // 분기: 예측 가능?
            apply_update(update);        // 호출: 무엇을 읽고/복사하고/할당?
        }
    }
}
```
루프 카운터 자체보다 **데이터 접근·분기·호출**을 먼저 의심하고, 측정으로 확인한다.

- ✅ 체크포인트: 둘 다 O(n) 확인 후 다음에 볼 것 → **한 iteration 안의 실제 작업량**.
- 🎤 "당신 O(n)이 왜 남의 O(n)보다 느린가?" → 각 루프의 한 iteration을 비교: 읽는 메모리, 복사/할당하는 객체, 호출하는 함수, 만나는 분기.
- 🎤 "이 hot path를 설명해봐" → 입력 하나를 루프에 태우고, 각 read/branch/call이 유발하는 일을 펼쳐 설명.
- 🎤 "어떻게 빠르게?" → 의심 비용을 코드에서 지목 → 메커니즘 설명 → 검증할 측정 방법 제시.

---

## 2. Memory Layout — vector vs linked list

**핵심:** 둘 다 O(n) 스캔이지만 `vector`는 **다음 원소 주소 = 현재 + sizeof(T)** (예측 가능), `list`는 **현재 노드를 읽어야 다음 주소를 앎** (포인터 체이싱).

| | `vector<MarketUpdate>` | `list<MarketUpdate>` | `vector<MarketUpdate*>` |
|---|---|---|---|
| 저장 | 한 개의 연속 블록 | 노드별 개별 할당 (+prev/next 포인터) | **포인터만** 연속, 객체는 흩어짐 |
| 스캔 | 순차 주소 → 캐시 라인 재사용 + HW prefetch | 매 스텝 의존적 로드 (pointer chasing) | 포인터 읽고 → 역참조 시 미스 가능 |

- 루프 본문이 같아도 차이는 **iterator 내부**에 숨어 있다 (vector: 옆으로 이동 / list: 포인터 따라감).
- `list`가 맞는 경우: **안정적 참조(stable reference)** 필요, 알려진 노드의 O(1) splice/erase — 단, hot path에서 전체 스캔하지 않을 때.
- ✅ 체크포인트: 객체 자체가 인접한 컨테이너 → `std::vector<MarketUpdate>`.
- 🎤 "vector 스캔이 왜 빠른가?" → 원소가 붙어 있어 CPU가 예측 가능한 순서로 가져옴. list는 매 노드마다 포인터를 따라가야 함.
- 🎤 "vector of pointers는 locality가 있나?" → 포인터는 연속이지만 객체는 흩어져 있을 수 있어 역참조마다 다른 메모리로 점프.

---

## 3. Cache Lines, Locality, Pointer Chasing

**핵심:** 캐시 미스 한 번 = **64바이트 캐시 라인 통째로** 가져옴 (x86-64 일반값). 가까운 바이트를 곧이어 쓰면 공짜에 가깝다 = **spatial locality**.

- `0x1000` 1바이트 로드 → `0x1000–0x103F` 전체가 캐시에 들어옴 → 이어서 `0x1004`, `0x1008` 읽기는 같은 라인 히트.
- 정확한 레이턴시는 캐시 레벨·CPU마다 다름 → **숫자는 측정으로**.
- 2D row-major 배열: `arr[i][j]` (안쪽 루프가 j) = 인접 접근 ✅ / `arr[j][i]` (안쪽이 행을 건너뜀) = 라인마다 미스 ❌.

### MarketUpdate와 캐시 라인
- 필드 8+8+8+4 = 28B → tail padding 4B → `sizeof == 32` (64-bit ABI 기준).
- 64B 라인 하나에 **업데이트 2개**. 버퍼가 64B 정렬 아니면 한 객체가 라인 경계를 넘을 수도 있음.
- `list`는 노드마다 링크 필드 + 불특정 위치 → 라인 활용도 ↓, 다음 주소를 알기 전까지 요청 불가.

### Locality 설계
- 질문: "한 iteration이 몇 바이트를 쓰고, 다음 iteration은 어디를 읽나?" → 가져온 라인 중 유용한 비율.
- 모든 필드를 읽는 루프 → 컴팩트 struct의 vector (AoS).
- `price`만 많이 읽는 루프 → 별도 dense `price` 배열 (SoA). 자주 읽는 필드를 묶기.
- ✅ 체크포인트: `0x1000` 읽고 `0x1008` 읽기 → 첫 접근이 가져온 **같은 캐시 라인에 이미 있음**.
- 🎤 "Pointer chasing이 뭔가?" → 현재 객체에서 포인터를 읽고 그 값을 다음 로드 주소로 씀. 현재 객체가 미스면 다음 요청 자체가 지연.
- 🎤 "두 필드만 읽는 hot loop의 레이아웃?" → 접근 패턴을 보고 두 필드를 더 적은 라인에 몰기 (hot 필드 그룹핑 / 별도 dense 배열) → 측정.

---

## 4. Allocation in the Hot Path

**핵심:** `push_back`은 보통 이미 가진 공간에 쓰지만, `size == capacity`인 **그 한 번**은 새 버퍼 할당 → 전체 이동 → 옛 버퍼 해제. 소스는 똑같이 생겼다.

- **자동 저장소(stack):** 함수 진입/복귀 시 스택 포인터 조정 한 번으로 일괄 관리.
- **힙 할당:** allocator가 블록을 개별 추적. 빠른 경로(스레드 로컬 캐시) vs 느린 경로(스레드 간 동기화, 내부 풀 리필, OS에 페이지 요청). → **분산(variance)이 문제**: 가끔 긴 경로가 평범한 틱에 떨어짐.
- **Amortized O(1)** = 평균. hot path 리뷰에서는 "*이번* iteration이 성장을 유발할 수 있나?"가 질문.

### `new`/`malloc`을 grep해도 안 보이는 할당
- `vector` 성장 (capacity 초과)
- `unordered_map` 삽입 = 노드 할당, load factor 초과 시 **bucket 배열 교체 + rehash**
- 문자열 연결 → 더 큰 문자 버퍼
- `std::function`에 SBO보다 큰 callable 저장

```cpp
// A: 루프 안에서 성장 → 어느 iteration에선가 재할당
std::vector<Order> log;
for (const Order& o : new_orders) log.push_back(o);

// B: 루프 전에 한 번에 확보
std::vector<Order> log;
log.reserve(new_orders.size());
for (const Order& o : new_orders) log.push_back(o);  // 복사는 여전히 발생
```
- `reserve`는 **재할당·이동만** 제거. 원소 복사/생성 비용은 남음.
- 배치마다 같은 버퍼 재사용: `clear()` = 원소 파괴, **capacity 유지** → 다음 배치 할당 없음. 확장판 = memory pool / arena.
- 정상 상태 목표: **처리 시작 전에 메모리 확보, 업데이트 도착 중엔 재사용**.
- ✅ 체크포인트: `size == capacity`에서 `push_back` → 더 큰 버퍼 할당 + 기존 원소 이전.
- 🎤 "이 루프에서 뭐가 할당하나?" → 컨테이너 성장, map 삽입/rehash, string 생성, `std::function` 같은 type-erased callable.
- 🎤 "reserve하면 push_back이 공짜?" → 아니다. 성장은 막지만 원소 생성/복사는 그대로.

---

## 5. Copies, Moves, Object Layout

**핵심:** 파라미터/루프 변수 **선언**이 복사 여부를 결정. `sizeof`는 *inline 필드*만 말해줄 뿐, 힙을 소유한 객체의 복사 비용은 말해주지 않는다.

```cpp
void on_update(MarketUpdate u);         // by value → lvalue 인자면 복사
void on_update(const MarketUpdate& u);  // 원본 참조, 복사 없음
```

### 레이아웃과 패딩
- ABI가 필드별 alignment 요구 → 컴파일러가 패딩 삽입 (배열에서 다음 원소도 정렬 유지).
- `MarketUpdate`: `qty`가 28B에서 끝남 + tail padding 4B = **32B**. 타깃/ABI 바뀌면 `sizeof` 재확인.

### 딥카피가 비싸지는 경우
```cpp
struct Order {
    std::uint64_t     id;
    std::vector<Fill> fills;  // 헤더(ptr/size/cap)만 inline, 원소는 힙
};
```
- `sizeof(Order)`는 fill이 3개든 1,000개든 동일 → 복사 비용 지표로 **틀림**.
- `Order b = a;` → `b.fills` 새 버퍼 할당 + 원소 1,000개 복사 생성 = **deep copy**.
- `Order b = std::move(a);` → `a.fills`의 버퍼 포인터를 넘겨받음, **fill 개수와 무관한 O(1)**. `a`는 *valid but unspecified* (파괴·재대입은 안전, `empty()` 가정 금지).

### `std::move`의 정체
- `std::move(a)` = **캐스트일 뿐** (lvalue → xvalue). 실제 이전은 move 생성자/대입이 수행.
- move 생성자가 없거나 `const` 객체면 → 조용히 **복사**로 떨어짐.
- C++17: `Order c = make_order();` → 반환된 prvalue가 `c`를 직접 생성 (copy/move 둘 다 없음).
- `return tmp;`는 그대로 두기 → NRVO 가능. `return std::move(tmp);`는 **NRVO를 막음** ❌.

### range-for도 복사한다
```cpp
for (Order o : orders)        { apply(o); }  // 매 iteration 딥카피 ❌
for (const Order& o : orders) { apply(o); }  // 복사 없음 ✅
```
- ✅ 체크포인트: fill 1,000개인 `Order`를 `std::move` → 버퍼 소유권 이전, `a`는 valid-but-unspecified.
- 🎤 "struct by-value가 비싼가?" → 크기와 **복사 생성자가 하는 일**을 봄. flat struct면 inline 필드 복사, vector 멤버가 있으면 할당 + 전 원소 복사.
- 🎤 "sizeof가 필드 합보다 큰 이유?" → alignment 패딩. MarketUpdate = 필드 28B + tail 4B.

---

## 6. Branches, Indirect Calls, Inlining

**핵심:** CPU는 파이프라인(fetch/decode/execute/retire 겹쳐 실행). 조건 분기에서 **추측 실행**하고, 틀리면 flush → **~15–20 cycle 버블**. 비용은 **데이터 패턴**에 달림.

- 업데이트 99%가 AAPL → 예측기가 학습, 분기 거의 공짜. 심볼이 랜덤하게 섞임 → 동전 던지기, 미스 페널티 빈발.

### Direct vs Indirect call
| | Direct `apply_update(u)` | Indirect `handler(u)`, `s->on_update(u)` |
|---|---|---|
| 타깃 | 명령어에 상대 오프셋 인코딩 (`call rel32`) | 메모리/레지스터에서 주소 로드 후 점프 |
| 예측 | 디코드 즉시 알 수 있음 | BTB(Branch Target Buffer)로 예측, 타깃이 바뀌면 미스 |
| **인라이닝** | 가능 | **불가 → 최적화 장벽** |

### 인라이닝이 진짜 핵심
- 인라인되면 call/ret·스택 프레임 제거 + **호출자 최적화 범위에 편입** (레지스터 재사용, 상수 전파, 중복 계산 제거, 벡터화).
- 간접 호출: 컴파일러는 callee가 **아무 메모리나 읽고 쓸 수 있다고 가정** → 레지스터 spill, 메모리 재로드, 루프 최적화 포기.

### 대응
```cpp
// 분기형: side가 랜덤하면 미스 빈발
if (u.side == Side::BUY) total_buy_qty += u.qty;
// branchless: 조건 점프 없음 (cmov/산술)
total_buy_qty += (u.side == Side::BUY) * u.qty;
```
- branchless는 **양쪽을 다 계산** → 99% 예측 가능한 분기라면 오히려 일반 분기가 빠를 수 있음. 진짜 랜덤일 때 이득.
- 배치 처리라면 **조건별로 정렬/파티셔닝** → buy 1,000개 → sell 1,000개면 예측기 ~100%.
- hot path 핸들러는 **concrete하게** (template/직접 호출). virtual·`std::function`은 설정/시작/루프 밖 라우팅용.
- ✅ 체크포인트: direct → 함수 포인터 호출로 바꿨을 때 가장 큰 비용 → **인라이닝·호출 경계 최적화 상실**.
- 🎤 "branchless는 언제?" → 결과가 진짜 예측 불가일 때. 예측 가능하면 일반 분기가 안 탄 경로를 건너뛰니 더 빠름.
- 🎤 "인라이닝이 call/ret 절약 외에 주는 것?" → callee를 호출자 최적화 범위로 → 레지스터 할당, 상수 전파, 중복 로드 제거.

---

## 7. Virtual Functions & Vtables

**핵심:** `s->on_update(u)` = vptr 로드 → vtable 슬롯 로드 → **간접 call**. 로드 두 번은 보통 L1 히트라 싸다. 진짜 비용은 **인라이닝 상실** + 타입이 섞이면 **BTB 미스**.

### 레이아웃
- 가상 함수가 있는 클래스마다 **vtable** 하나 (함수 포인터 정적 배열, `.rodata`), 메서드별 고정 인덱스.
- 각 객체 첫 멤버로 숨은 **vptr** (8B, 8B 정렬 강제).
- `int` 하나(4B)짜리 클래스에 virtual 추가 → 4B → **16B** (vptr 8 + int 4 + 패딩 4). 객체가 수백만 개면 힙·캐시 footprint 증가.

```asm
movq (%rdi), %rax   ; 객체에서 vptr 로드
movq (%rax), %rax   ; vtable 슬롯 0에서 함수 포인터 로드
callq *%rax         ; 간접 호출
```
- 같은 타입만 반복 → BTB 학습, 거의 안 멈춤. Momentum/MeanReversion/MarketMaker가 번갈아 → 매번 타깃 변경 → 미스 15–20 cycle.

### Hot path에서 virtual 피하기
```cpp
// 1) 템플릿 정적 디스패치 (타입을 컴파일 타임에 앎) — CRTP도 같은 계열
template <typename StrategyImpl>
void run_strategy(StrategyImpl& s, const std::vector<MarketUpdate>& ups) {
    for (const auto& u : ups) s.on_update(u);   // direct → inline
}

// 2) 닫힌 타입 집합: std::variant + std::visit (vtable·힙 포인터 없음)
using StrategyVariant = std::variant<MomentumStrategy, MeanReversionStrategy>;
for (auto& s : strategies)
    std::visit([&](auto& c) { c.on_update(update); }, s);  // 태그 switch, 분기마다 inline 가능

// 3) 루프 뒤집기: virtual 호출을 업데이트당 → 배치당 1회로
strategy->process_batch(updates);  // 내부 루프는 direct/inline
```
- ✅ 체크포인트: 로드 2번 외에 virtual이 느린 이유 → **컴파일러가 타깃을 인라인 못 해서** 레지스터 재사용·호출 경계 최적화 불가.
- 🎤 "virtual 호출에서 컴파일러가 생성하는 것?" → offset 0의 vptr 로드 → vtable 고정 인덱스의 함수 포인터 로드 → 간접 call.
- 🎤 "virtual 없이 런타임 다형성?" → 닫힌 집합이면 `variant`+`visit`, 컴파일 타임에 결정되면 template/CRTP.

---

## 8. Type Erasure — `std::function`, `std::any`

**핵심:** `std::function` = 상태 + 코드를 담는 **type-erased** 래퍼. 호출은 항상 **간접 점프(인라인 불가)**, 캡처가 크면 **생성 시 힙 할당**.

### 내부 구조 (대략, 보통 32 또는 48B)
- `invoke_fn` : 구체 타입용 **trampoline** 포인터 (storage를 원래 타입으로 캐스트 후 `operator()` 호출)
- `manager_fn`: 복사/이동/파괴 담당
- `storage[16~24]`: **SBO (Small-Buffer Optimization)** 인라인 버퍼
- raw 함수 포인터 `void(*)(const MarketUpdate&)`는 코드 주소만 → 캡처 상태를 못 담음.

### SBO와 할당 위험
- callable이 인라인 버퍼(구현별 16–24B: libstdc++/libc++/MSVC)에 들어가고 nothrow-move 가능 → **할당 없음**.
- 넘으면 `operator new` → 루프 안에서 `std::function`을 생성/재바인딩하면 **힙 할당 + 락**.
```cpp
auto small = [&engine](const MarketUpdate& u) { engine.process(u); }; // 참조 1개 캡처 → SBO
struct Params { char symbol[16]; double weights[4]; };                 // 48B
auto big = [params](const MarketUpdate& u) { /*...*/ };                // 값 캡처 → 힙
```
- 할당이 없어도 **호출은 여전히 간접 점프** → 인라인·벡터화·상수 전파 차단.

### `std::any`
- 임의 값용 type erasure: 인라인 버퍼 + 힙 폴백 + 타입 descriptor. `any_cast<T>`는 런타임 `typeid` 비교 (불일치 시 throw).
- 타입 집합이 유한하면 **`std::variant`** (tagged union, 힙 폴백 없음, 런타임 typeid 검사 없음).

### Hot path에서 erasure 제거
1. **템플릿 콜백:** `template <typename Callback> void process(..., Callback&& cb)` → 호출 지점마다 인스턴스화 → inline.
2. **상태 없는 콜백 → 함수 포인터:** 간접 호출이지만 **할당 0 보장**.
3. **`function_ref` (non-owning):** 포인터 2개(callable 주소 + trampoline), 소유·할당 없음. 호출 기간 동안만 살아 있는 콜백에.
- ✅ 체크포인트: 업데이트마다 `std::function` 호출이 템플릿 람다보다 느린 이유 → 항상 간접 점프 + 인라인 차단, 큰 캡처는 힙 할당.
- 🎤 "`std::function` vs raw 함수 포인터?" → 포인터는 코드 주소뿐. `std::function`은 trampoline 포인터로 수명·호출을 관리하며 상태 있는 람다/펑터 저장.

---

## 9. Benchmarking Without Lying

**핵심:** 0.3ns(1 cycle 미만)가 나왔다면 **코드가 안 돈 것**. 컴파일러는 측정의 적이다.

### 컴파일러의 속임수
| 함정 | 무슨 일 | 대응 |
|---|---|---|
| **Dead-code elimination** | 결과를 안 쓰면 루프째 삭제 | `benchmark::DoNotOptimize(x)` = `asm volatile("" : "+r,m"(x) : : "memory")` |
| **Constant folding** | 컴파일 타임 상수 입력 → 한 번 계산 후 결과만 박음 | 미리 로드한 실제/합성 데이터 버퍼를 순회 |
| **-O0/-O1로 낮추기** | 스택 spill 투성이, 프로덕션과 무관 | ❌ 하지 말 것 |

- `asm volatile` 배리어: "불투명한 asm이 `val`을 읽고 수정한다" + `"memory"` clobber로 메모리 재배치 금지 → 실제 명령 생성 강제.

### 캐시·하드웨어 노이즈
- cold 1패스 = 알고리즘이 아니라 **페이지 폴트·메모리 컨트롤러·L3 미스**를 측정.
- 1KB 버퍼 1천만 번 = L1(32KB)에 고정 + 비현실적으로 반복적인 분기 패턴.
- 워밍업 패스(캐시·예측기 학습) 후 steady-state 구간만 타이밍.
- 환경: 스레드 코어 고정(`pthread_setaffinity_np`), CPU 주파수 고정(Turbo Boost off), `isolcpus`로 코어 격리, NUMA 이동 방지.
- 셋업·할당·로그는 **타이밍 구간 밖**. `std::cout` 하나가 ms 단위 I/O 동기화.

### 평균 vs 꼬리
- 999건 25ns + 1건 80µs(페이지 폴트) → 평균 ~105ns로 "깨끗해" 보임. 그러나 그 80µs가 **패킷 드롭 / stale 가격 체결**을 만든다.
- 트레이딩 평가 = **p50(전형) + p99/p99.9(최악 버스트)**. 평균 30ns라도 p99.9에서 10µs 스파이크면 변동성 폭증 때 큐 우선순위를 잃는다.
- ✅ 체크포인트: 수학 많은 파서가 0.1ns/call → 결과를 안 읽어서 **컴파일러가 계산 제거**.
- 🎤 "마이크로벤치마크 노이즈 격리?" → 격리 코어에 고정, 주파수 고정(turbo/thermal off), 워밍업, 동적 데이터로 상수 폴딩 방지.

---

## 10. Cost Model Checkpoint (모듈 1 총정리)

### 5대 체크리스트
1. **레이아웃·stride:** 64B 라인. 연속 스윕 → HW prefetcher가 미리 스트리밍. 포인터 체이싱 → 스텝마다 DRAM(~50–70ns).
2. **정상 상태 할당:** p50 20ns여도 arena 확장 한 번에 40µs. hot loop는 할당 0 (`reserve`, 스크래치 버퍼 재사용).
3. **복사·소유권:** 읽기 전용은 `const&`, 소유권 이전은 move, hot struct는 컴팩트하게.
4. **분기·간접 디스패치:** 예측 가능 분기 < 1 cycle, 불규칙 15–20 cycle. 간접 호출은 최적화 장벽 → branchless/template/variant.
5. **측정 위생:** 생성된 어셈블리로 sanity check, DoNotOptimize, 워밍업, I/O 분리, p99/p99.9.

### 낯선 루프 리뷰 순서
inner loop 분리 → 메모리 접근 감사(연속? 간접?) → 수명·복사(by-value, 임시 string/vector, reserve 없음) → 디스패치 경계(virtual, 함수 포인터, 데이터 의존 분기) → **각 비용마다 구조적 대안 제시**.

### Worked example — 틱 핸들러 감사
```cpp
void handle(const std::vector<MarketUpdate>& updates,
            std::vector<Strategy*>& strategies) {
    std::vector<Order> log;                  // ④ 빈 채로 시작 → 성장 재할당
    for (MarketUpdate u : updates) {         // ① by-value: 매 틱 32B 복사
        if (u.symbol[0] == 'A') {            // ② 심볼이 섞이면 예측 실패
            for (Strategy* s : strategies)
                s->on_update(u);             // ③ virtual: 인라인 불가
            log.push_back(make_order(u));    // ④ 동적 성장
        }
    }
}
```
| # | 문제 | 수정 |
|---|---|---|
| ① | by-value 복사 | `const MarketUpdate& u` |
| ② | 불규칙 분기 | 사전 정렬/파티셔닝, 또는 한 심볼에 집중된 데이터 |
| ③ | virtual 디스패치 | 전략 집합을 알면 template 또는 `std::variant` |
| ④ | 동적 성장 | `log.reserve(updates.size())` |

→ Big-O는 그대로 O(n)인데 **iteration당 수백 cycle** 절감.

- ✅ 체크포인트: 10만 건 12ms(120ns/틱), 어디서 시간이 새나? → 루프 본문의 비연속 접근·할당·복사·간접 호출을 점검하고 **profiler 카운터로 stall 확인**. (-O3/fast-math, 무작정 AVX-512, list로 교체 ❌)
- 🎤 "사소한 연산인데 iteration당 100ns?" → 3GHz면 300 cycle. 산술은 < 5 cycle. 나머지는 캐시 미스 stall, allocator 락, 분기 미스, 인라인 막는 간접 호출.
- 🎤 "cycle이 어디 쓰이는지 진단?" → `perf stat`으로 `cache-misses`, `L1-dcache-load-misses`, `branch-misses`. **IPC가 낮으면** 메모리/분기 복구에 stall.

---

## 11. Module 1 Interview Drill

> 공통 패턴: **iteration당 비용을 지목 → 처방을 지목 → 숫자 지어내지 않기.**

### Prompt 1 — 두 O(n) 루프, 속도 2배 차이
- 가능한 설명 (정답 A·B·C·E): 느린 메모리 접근 패턴 / 본문의 할당·로그·복사·숨은 호출 / 예측 어려운 분기·간접 호출 / 벤치마크가 셋업이나 죽은 코드를 잼.
- ❌ 나쁜 답: "둘 다 O(n)이니 비슷" · "아마 캐시 문제" (버즈워드 점프) · "벡터화 때문에 10배" (숫자·원인 날조).
- ✅ 답변 골격: "둘 다 O(n)이니 iteration당 비용을 비교하겠다. hot path를 찾고, 각 iteration이 무엇을 읽고·할당하고·호출하는지, CPU가 예측 가능한지 본다. 제일 먼저 Big-O에 안 보이는 숨은 일 — 함수 호출, 문자열 연결, 컨테이너 삽입 — 을 찾겠다."

### Prompt 2 — 최근 n개 업데이트를 매 틱 순회: vector vs list?
- 정답: A(연속) · B(노드 개별 할당) · E(대량 O(1) splice면 list).
- ❌ "무조건 vector" (이유·예외 없음) · "list는 메모리를 안 옮겨서 빠르다" (스캔은 대역폭 문제).
- ✅ "스캔 위주 hot path면 `vector<MarketUpdate>`. 연속 블록이라 CPU가 스트리밍. list는 매 스텝이 allocator가 둔 곳으로의 새 여행. list는 긴 시퀀스 O(1) splice나 삽입 중 안정 포인터가 필요할 때만 — 여기엔 해당 없음."

### Prompt 3 — 왜 가까운 메모리가 싼가?
- 정답: A(64B 캐시 라인) · B(라인 안 읽기는 훨씬 쌈) · D(라인당 업데이트 ~2개).
- ❌ "L1 4 cycle, DRAM 200 cycle" — 타깃에서 측정 안 한 숫자 인용 금지. "몇 배 빠르다, 측정해보겠다"로. · "하드웨어마다 다르다" (비답변).
- ✅ "CPU는 64B 라인 단위로 읽는다. 업데이트 i를 담은 라인을 가져오면 32B짜리 i+1이 보통 같은 라인 → 새 fetch 없음. list는 노드마다 새 라인·새 fetch 가능."

### Prompt 4 — 빈 `unordered_map`에 주문 10,000개 삽입, 숨은 비용?
- 정답: A(삽입마다 노드 할당) · B(load factor 초과 시 rehash: 새 bucket 배열 + 전 원소 재삽입) · D(`reserve(10000)`으로 대부분 제거).
- ❌ "new 안 썼으니 할당 없음" · "할당은 요즘 빠름" (문제는 꼬리와 locality 손실) · "커스텀 allocator" (도구는 맞지만 **순서가 틀림** — 먼저 할당을 짚고 → reserve/재사용 → 요청 시 더 깊게).
- ✅ "숨은 비용 둘: 삽입마다 노드 할당, load factor 초과 시 rehash. `orders.reserve(10000)`으로 bucket 배열을 한 번에 잡고, 더 가려면 배치마다 새로 만들지 말고 `clear()`로 재사용."

---

## 용어 사전

| 용어 | 한 줄 정의 |
|---|---|
| Hot path | 주 워크로드 처리 중 반복 실행되는 코드 경로 |
| Cost model | iteration 내부 연산을 추적해 지배적 비용을 찾는 사고틀 |
| Cache line | 캐시 전송 단위, x86-64에서 보통 64B |
| Spatial locality | 가까운 주소를 시간적으로 가깝게 접근 |
| Pointer chasing | 현재 객체에서 읽은 포인터로 다음 로드 주소를 결정 |
| Amortized O(1) | 평균 비용. 개별 호출은 비쌀 수 있음 |
| Deep copy | 외부 객체 + 소유한 동적 데이터까지 복사 |
| xvalue / `std::move` | 이동 가능 표시 캐스트. 실제 이동은 move 생성자 |
| NRVO | 이름 있는 지역 변수를 반환 슬롯에 직접 생성 |
| Pipeline bubble | 분기 미스 후 flush로 생기는 실행 공백 (~15–20 cycle) |
| BTB | 간접 분기 타깃 예측 버퍼 |
| vtable / vptr | 가상 함수 포인터 테이블(.rodata) / 객체 내 숨은 포인터 |
| SBO | 작은 객체를 인라인 버퍼에 저장해 힙 할당 회피 |
| Type erasure | 구체 타입을 숨기고 간접 포인터로 균일 인터페이스 제공 |
| DCE | 결과가 안 쓰이는 코드를 컴파일러가 삭제 |
| Tail latency | p99/p99.9 등 분포 꼬리의 지연 |
| IPC | Instructions per cycle, 낮으면 stall 의심 |
