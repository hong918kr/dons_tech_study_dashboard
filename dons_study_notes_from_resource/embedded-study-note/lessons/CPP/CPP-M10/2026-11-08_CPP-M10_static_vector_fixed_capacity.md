---
id: CPP-M10
time: 90분
updated: 2026-11-08
---
# CPP-M10 ETL 스타일 static_vector<T, N> 구현

## 학습 목표

- `alignas(T) std::byte` 저장소와 `std::construct_at` / `std::destroy_at` 으로 **힙 없는 고정 용량 벡터**를 만들고, "칸 `[0, size)` 만 객체가 살아 있다"는 불변식을 모든 연산에서 지킨다.
- `erase` / `insert` 에서 **대입할 칸과 생성할 칸, 소멸할 칸을 구분**해, 수명 위반 없이 원소를 민다. 인자가 자기 원소를 가리키는 별칭(aliasing) 삽입까지 처리한다.
- C++20 조건부 trivial 특수 멤버 함수(`~static_vector() requires ... = default;`)를 쓰고, trait 와 실제 어셈블리로 확인한다.
- 용량 초과(런타임 상황)와 전제조건 위반(호출자 버그)을 구분해 처리하고, `assert` / `std::abort` 가 Cortex-M 바이너리에 끌고 오는 크기를 직접 잰다.
- 복사·이동 횟수를 표로 예측하고 카운터 타입으로 검증한다. 전역 객체의 초기화(`.bss` vs `.init_array`)를 ELF 에서 확인한다.

## 1. 문제

펌웨어에서는 힙을 금지하는 경우가 많아서(CPP-B14 참고) `std::vector` 를 못 쓰지만, "개수가 변하는 목록"은 어디에나 있다 — 대기 중인 CAN 프레임, 활성 타이머, 연결된 BLE 피어. 면접에서는 "heap 없이 vector 를 구현해 보라"가 **객체 수명(lifetime)을 실제로 이해하는지**를 가장 빠르게 드러내는 문제다.

`std::array<T, N>` (CPP-B10 참고)로 안 되는 이유는 분명하다. 배열은 N개 전부를 처음부터 생성하고 끝까지 살려 둔다. `T` 에 기본 생성자가 없으면 만들 수도 없고, 안 쓰는 칸에도 생성자·소멸자가 돈다. `static_vector` 는 저장 공간만 미리 잡아 두고 객체는 필요할 때 그 자리에 만든다. 저장소 규칙(`alignas`, placement new, `std::launder`) 자체는 CPP-M07 이 담당하므로, 이 레슨은 그 위에 올리는 **컨테이너**에 집중한다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">static_vector&lt;T, 6&gt; — 크기 3일 때의 저장소</text>
  <g stroke="currentColor">
    <rect x="20" y="50" width="100" height="56" class="f-ok"/>
    <rect x="120" y="50" width="100" height="56" class="f-ok"/>
    <rect x="220" y="50" width="100" height="56" class="f-ok"/>
    <rect x="320" y="50" width="100" height="56" class="f-mut"/>
    <rect x="420" y="50" width="100" height="56" class="f-mut"/>
    <rect x="520" y="50" width="100" height="56" class="f-mut"/>
    <rect x="630" y="50" width="90" height="56" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="70" y="75">T 객체</text><text x="170" y="75">T 객체</text><text x="270" y="75">T 객체</text>
    <text x="70" y="95" font-size="12">살아 있음</text><text x="170" y="95" font-size="12">살아 있음</text><text x="270" y="95" font-size="12">살아 있음</text>
    <text x="370" y="75">raw</text><text x="470" y="75">raw</text><text x="570" y="75">raw</text>
    <text x="370" y="95" font-size="12">객체 없음</text><text x="470" y="95" font-size="12">객체 없음</text><text x="570" y="95" font-size="12">객체 없음</text>
    <text x="675" y="75">size_</text><text x="675" y="95">3</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="126">↑ begin()</text>
    <text x="320" y="126">↑ end() = construct_at 이 다음에 쓸 칸</text>
  </g>
  <text x="10" y="162" fill="currentColor">살아 있는 칸: 대입(operator=)·읽기·소멸 가능. 생성(construct_at)하면 이중 생성.</text>
  <text x="10" y="184" fill="currentColor">raw 칸: 생성만 가능. 대입·읽기·소멸은 전부 수명 위반(UB).</text>
  <text x="10" y="206" fill="currentColor">모든 멤버 함수의 일 = 끝날 때 이 그림이 다시 참이 되게 하는 것.</text>
  <text x="10" y="236" fill="currentColor" font-size="12">저장소는 alignas(T) std::byte[N*sizeof(T)] — 객체 자체는 벡터 안에 그대로 있다(힙 포인터 없음).</text>
</svg>
```

호스트에서 수명 위반은 보통 "조용히 돌아간다". 그래서 하네스의 `Tracked` 타입은 생성·소멸·대입을 전부 세면서, **살아 있는 주소의 장부**를 들고 죽은 칸에 대입하거나 이중 소멸하면 `g.bad` 를 올린다. 테스트는 값만이 아니라 "대입 몇 번, 생성 몇 번, 소멸 몇 번, bad 0"까지 표로 검사한다.

### Part A — 저장소와 끝쪽 연산

- `emplace_back(args...)` : `T*` 반환(꽉 찼으면 정책 호출 후 `nullptr`), `push_back` 은 그 위에 얹은 `bool` 버전.
- `pop_back()`, `clear()`, 소멸자: 살아 있는 원소만 소멸.
- 반복자는 그냥 `T*` — `std::ranges::contiguous_range` 를 만족해 `std::span` (CPP-B11 참고)으로 바로 넘어간다.
- `T` 는 기본 생성자 없는 타입, 이동 전용 타입, `const` 멤버 타입, over-aligned 타입이어도 된다. `N == 0` 도 컴파일돼야 한다.

### Part B — 면접관이 파고드는 부분

- `erase(pos)`, `erase(first, last)`, `emplace(pos, args...)`, `insert`: 민 칸의 수명을 정확히 관리. `v.insert(v.begin(), v[2])` 같은 별칭 삽입도 맞아야 한다.
- 복사/이동 생성·대입: 표로 정해진 횟수만큼만 복사/이동/대입/소멸.
- 조건부 trivial: `T` 가 trivial 이면 `static_vector<T,N>` 도 trivially copyable / trivially destructible.
- 용량 초과는 정책 템플릿 인자로: `ReturnOnFull`(반환값 확인) / `TrapOnFull`(즉시 정지). 실패한 호출은 **인자를 건드리지 않는다.**

### Part C — CAN 송신 대기열

하드웨어 메일박스가 꽉 차서 대기 중인 프레임의 큐. 각 `TxFrame` 은 버퍼 풀의 칸을 **이동 전용 RAII 핸들** `Lease` 로 소유한다(CPP-B06/B07 참고). 수명 관리가 틀리면 곧바로 **버퍼 누수 또는 이중 반납**으로 드러난다.

- `enqueue(TxFrame&&)`: id 오름차순(작을수록 높은 우선순위) 정렬 유지, 같은 id 는 FIFO. 꽉 찼으면 `false` 이고 호출자의 프레임은 그대로.
- `pop_next(TxFrame& out)`: 맨 앞을 꺼낸다.
- `expire(now_ms)`: 마감 지난 프레임을 모두 버린다(32비트 wrap-around 안전 비교).

## 2. Problem (English)

> **Implement `static_vector<T, N, Overflow>`: a heap-free, fixed-capacity vector.**
>
> ```cpp
> template <typename T, std::size_t N, typename Overflow = ReturnOnFull>
> class static_vector;
> ```
>
> Requirements:
>
> 1. Inline storage only (no heap, no `std::array<T,N>`): `T` need not be default-constructible, and exactly `size()` objects are alive at any time.
> 2. API: `emplace_back`, `push_back` (const& and &&), `pop_back`, `clear`, `emplace(pos, args...)`, `insert`, `erase(pos)`, `erase(first, last)`, `data`, `size`, `capacity`, `empty`, `full`, `operator[]`, `front`, `back`, iterators (`T*`), reverse iterators, `operator==`.
> 3. On a full container, `emplace_back`/`emplace` return `nullptr` and `push_back` returns `false`, after calling `Overflow::on_overflow()`. A failed call must not construct anything and must not move from its argument. Precondition violations (`pop_back` on empty, `erase(end())`) are `assert`s.
> 4. `erase` and `insert` must not assign to raw slots or construct over live objects. Inserting a reference to one of the container's own elements must work.
> 5. Copy and move are element-wise: assignment re-uses live elements, constructs into raw slots, and destroys any surplus. The moved-from container keeps its size.
> 6. When `T` is trivially destructible the container must be trivially destructible; when `T` is trivially copyable the container must be trivially copyable. Prove it with `<type_traits>`.
> 7. Assume `-fno-exceptions -fno-rtti`.
>
> Example: for `static_vector<int, 4> v{1, 2, 3}`, `v.erase(v.cbegin())` leaves `{2, 3}` and returns `v.begin()`; `v.insert(v.cbegin(), v[1])` leaves `{3, 2, 3}`; any further `push_back` returns `false` once the size reaches 4.
>
> Then build a CAN TX pending queue on top of it, where frames own a pool buffer through a move-only RAII handle.

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 대비
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
# 처음엔 "88/220 passed" — TODO 를 채워 220/220 을 만든다
```

## 4. 힌트

<details>
<summary>힌트 1 — emplace_back 은 construct_at 한 줄이 핵심</summary>

`full()` 확인 → `std::construct_at(slot_(size_), std::forward<Args>(args)...)` → 크기 증가. 생성이 끝난 **뒤에** 크기를 올려야 "`[0, size)` 는 살아 있다"가 항상 참이다. 빈 칸의 주소를 얻는 `slot_(i)` 는 이미 있다.

</details>

<details>
<summary>힌트 2 — erase 는 "밀기 → 끝 칸 소멸" 순서</summary>

지울 칸을 먼저 소멸시키면 그 칸은 raw 가 되어 대입할 수 없다. `std::move(p + 1, end(), p)` 로 살아 있는 칸끼리 대입하면 맨 끝 칸이 moved-from 으로 남는다. 소멸시킬 것은 그 칸이다.

</details>

<details>
<summary>힌트 3 — insert 의 끝 칸은 raw 다</summary>

한 칸씩 뒤로 밀 때 맨 끝 칸(`size_` 번째)에는 아직 객체가 없다. 거기만 `construct_at`, 나머지는 `std::move_backward` 로 대입. 마지막으로 `pos` 칸에 대입한다.

</details>

<details>
<summary>힌트 4 — 인자가 내 원소를 가리킨다면?</summary>

밀기 전에 인자로 임시 객체를 먼저 만들어 두면 된다. 다만 `pos == end()` 라면 밀 게 없으니 임시 객체 없이 `emplace_back` 으로 보낸다(이동 한 번 절약, 하네스가 횟수를 확인한다). 꽉 찼는지는 임시 객체를 만들기 **전에** 검사한다.

</details>

<details>
<summary>힌트 5 — 복사/이동 네 개를 helper 하나로</summary>

`assign_range_(It src, size_type n)` 하나면 된다. 앞쪽 `min(size_, n)` 칸은 대입, 그다음 `n` 까지는 생성, 남는 칸은 소멸. 복사 생성자는 `size_ == 0` 에서 시작하므로 전부 생성이 되고, `std::make_move_iterator(o.begin())` 를 넘기면 이동 버전이 된다.

</details>

<details>
<summary>힌트 6 — 조건부 trivial 은 선언 두 개</summary>

`~static_vector() requires kTrivialDtor = default;` 와 `~static_vector() { clear(); }` 를 나란히 쓴다. 복사·이동은 제약끼리 겹치면 모호하므로 일반 버전에 `!kTrivial...` 을 넣어 서로 배타적으로 만든다.

</details>

## 5. 풀이

### Part A — 불변식 하나로 끝까지

**저장소.** `alignas(T) std::byte storage_[N != 0 ? N * sizeof(T) : 1];`. `N == 0` 이면 크기 0 배열은 ill-formed 라 1바이트를 둔다. `std::byte` 배열은 다른 객체의 저장소가 될 수 있고, 그 안에 생성된 객체를 쓰는 규칙은 CPP-M07 에서 다뤘다.

**`slot_` 과 `data()` 를 나눈 이유.** `std::launder(p)` 의 전제조건은 "그 주소에 수명 중인 `T` 객체가 있을 것"이고 아니면 UB 다 ([ptr.launder]). 그래서 `slot_(i)` 는 launder 없는 주소 계산으로 `construct_at` 대상에만 쓰고, 읽는 쪽 `data()` 는 `size_ ? std::launder(slot_(0)) : slot_(0)` 으로 비어 있을 때를 피한다. (비어 있을 때의 `begin()` 은 비교만 할 뿐 역참조하지 않는다.)

`data() + i` 로 다음 원소에 가는 산술은 엄밀히는 논쟁 지점이다. 배열이 아닌 곳에서 포인터 산술을 하는 문제로 CWG 2182 ("Pointer arithmetic in array-like containers")가 있고 상태는 아직 drafting 이다. C++20 의 implicit object creation ([intro.object]) 으로 `std::byte` 배열 위의 `T[N]` 이 암묵적으로 생긴다고 보는 해석이 있지만, 표준 조문만으로 깔끔히 닫혔다고 단언하지는 않겠다. `std::vector` 구현도 같은 상황이며 실제 컴파일러는 이를 문제 삼지 않는다.

| 연산 | 원소 수 변화 | 비용 | 수명 |
|---|---|---|---|
| `emplace_back` | +1 | O(1) | 끝 칸 생성 |
| `pop_back` | −1 | O(1) | 끝 칸 소멸 |
| `clear` | −n | O(n) (trivial 이면 0) | 전부 소멸 |
| `erase(pos)` | −1 | O(n−pos) | 대입 후 끝 칸 소멸 |
| `insert(pos)` | +1 | O(n−pos) | 끝 칸 생성 + 대입 |

**`push_back(v[0])` 은 왜 안전한가.** `std::vector` 는 재할당 때문에 별칭 처리가 어렵지만, 여기선 재할당이 없다. 새 칸은 raw 이고 인자는 살아 있는 다른 칸이다.

**`emplace_back` 의 작은 함정 — 다시 읽기.** 처음엔 `construct_at(slot_(size_), ...); ++size_;` 로 썼다. Cortex-M 출력에 `size_` 를 **두 번 읽는** 코드가 나왔다. `T = int` 의 저장이 `unsigned int` 멤버 `size_` 와 별칭일 수 있어서(signed/unsigned 짝은 aliasing 허용, [basic.lval]) 컴파일러가 다시 읽는 것이다. `const size_type n = size_;` 로 복사하자 사라졌다. 고친 뒤의 clang Thumb-2 실제 출력:

```text
sv_push:
	ldr	r2, [r0, #32]
	cmp	r2, #8
	ittt	ne
	strne.w	r1, [r0, r2, lsl #2]
	addne	r1, r2, #1
	strne	r1, [r0, #32]
	subs.w	r0, r2, #8
	it	ne
	movne	r0, #1
	bx	lr
```

### Part B — 민 칸의 수명 관리

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">erase(pos=1): 대입으로 밀고, 끝 칸만 소멸 (크기 4 → 3)</text>
  <g stroke="currentColor">
    <rect x="20" y="34" width="70" height="36" class="f-ok"/><rect x="90" y="34" width="70" height="36" class="f-warn"/><rect x="160" y="34" width="70" height="36" class="f-ok"/><rect x="230" y="34" width="70" height="36" class="f-ok"/>
    <rect x="420" y="34" width="70" height="36" class="f-ok"/><rect x="490" y="34" width="70" height="36" class="f-ok"/><rect x="560" y="34" width="70" height="36" class="f-ok"/><rect x="630" y="34" width="70" height="36" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="55" y="57">A</text><text x="125" y="57">B</text><text x="195" y="57">C</text><text x="265" y="57">D</text>
    <text x="455" y="57">A</text><text x="525" y="57">C</text><text x="595" y="57">D</text><text x="665" y="57">raw</text>
  </g>
  <text x="330" y="57" fill="currentColor">→</text>
  <text x="20" y="92" fill="currentColor" font-size="12">B ← C, C ← D 는 살아 있는 칸끼리 대입(2회)</text>
  <text x="420" y="92" fill="currentColor" font-size="12">마지막 칸의 moved-from D 를 destroy_at(1회)</text>
  <text x="10" y="130" fill="currentColor" font-weight="bold">emplace(pos=1, X): 끝 칸만 생성, 나머지는 대입 (크기 3 → 4)</text>
  <g stroke="currentColor">
    <rect x="20" y="144" width="70" height="36" class="f-ok"/><rect x="90" y="144" width="70" height="36" class="f-ok"/><rect x="160" y="144" width="70" height="36" class="f-ok"/><rect x="230" y="144" width="70" height="36" class="f-mut"/>
    <rect x="420" y="144" width="70" height="36" class="f-ok"/><rect x="490" y="144" width="70" height="36" class="f-acc"/><rect x="560" y="144" width="70" height="36" class="f-ok"/><rect x="630" y="144" width="70" height="36" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="55" y="167">A</text><text x="125" y="167">B</text><text x="195" y="167">C</text><text x="265" y="167">raw</text>
    <text x="455" y="167">A</text><text x="525" y="167">X</text><text x="595" y="167">B</text><text x="665" y="167">C</text>
  </g>
  <text x="330" y="167" fill="currentColor">→</text>
  <g fill="currentColor" font-size="12">
    <text x="20" y="204">① T tmp(args...) — 임시 객체 먼저 (args 가 내 원소를 가리킬 수 있다)</text>
    <text x="20" y="224">② construct_at(끝 칸, move(C)) — raw 칸은 생성(이동 생성 1회)</text>
    <text x="20" y="244">③ move_backward: B 를 C 자리로 — 살아 있는 칸은 대입</text>
    <text x="20" y="264">④ d[pos] = move(tmp) — 대입, 끝나면 tmp 소멸 1회</text>
  </g>
  <text x="10" y="296" fill="currentColor">규칙: raw 칸에는 생성, 살아 있는 칸에는 대입, 남는 칸은 소멸. 이 셋을 섞으면 UB.</text>
  <text x="10" y="318" fill="currentColor" font-size="12">pos == end() 면 밀 것이 없으므로 임시 객체 없이 곧바로 emplace_back.</text>
</svg>
```

C++26 `std::inplace_vector` 의 초안 문구 ([inplace.vector.modifiers])도 erase 의 복잡도를 "소멸자는 지운 개수만큼, 대입 연산자는 지운 원소 뒤의 원소 수만큼 호출"로 규정한다. 우리 표와 똑같다.

하네스가 확인하는 실제 횟수 (전부 PASS):

| 호출 | 대입(이동) | 이동 생성 | 소멸 | 비고 |
|---|---|---|---|---|
| `erase(1)` (크기 5) | 3 | 0 | 1 | 끝 칸 하나만 소멸 |
| `erase(4)` (크기 5, 맨 끝) | 0 | 0 | 1 | |
| `erase(1, 3)` (크기 5) | 2 | 0 | 2 | |
| `emplace(0, 9)` (크기 3) | 3 | 1 | 1 | 임시 1번 생성 후 소멸 |
| `emplace(3, 9)` (`pos == end`) | 0 | 0 | 0 | 임시 없음, 일반 생성 1회 |

**복사/이동.** `assign_range_` 하나로 네 함수의 본문을 처리한다.

| 대입 전 크기 (dst → src) | 대입 | 생성 | 소멸 |
|---|---|---|---|
| 0 → 3 | 0 | 3 | 0 |
| 2 → 3 | 2 | 1 | 0 |
| 3 → 3 | 3 | 0 | 0 |
| 4 → 2 | 2 | 0 | 2 |
| 2 → 0 | 0 | 0 | 2 |

**이동의 의미.** 포인터를 훔칠 수 없으니 이동도 원소마다 한 번이고, O(n) 이다(CPP-M09 참고). 이동 후 **원본의 크기는 유지**되고 원소는 moved-from 이다. `std::array` 와 같은 동작이고, `T` 가 trivial 이면 defaulted 이동이 비트 복사(크기 유지)이므로 trivial 경로와도 일관된다. 재사용하려면 `clear()` 부터.

**조건부 trivial (C++20, P0848).** 소멸자를 두 개 선언하면 클래스가 완성될 때 제약이 만족된 후보 중 **가장 제약이 강한 것**이 진짜 소멸자가 된다([class.dtor]). 복사·이동도 같은 방식이다. `std::inplace_vector` 초안([inplace.vector.overview])의 조건을 그대로 따랐다.

| 조건 | 결과 |
|---|---|
| `is_trivially_copy_constructible_v<T>` | 복사 생성자 trivial |
| `is_trivially_move_constructible_v<T>` | 이동 생성자 trivial |
| `is_trivially_destructible_v<T>` | 소멸자 trivial |
| 소멸자 trivial + 복사 생성 trivial + 복사 대입 trivial | 복사 대입 trivial |
| 소멸자 trivial + 이동 생성 trivial + 이동 대입 trivial | 이동 대입 trivial |

trait 는 하네스가 CHECK 하고(`is_trivially_copyable_v<static_vector<int,4>>`, `!is_trivially_destructible_v<static_vector<Tracked,4>>` 등), 생성된 코드는 `arm-none-eabi-g++` 로 봤다. `int` 는 소멸 코드가 아예 없고, 소멸자 있는 `NT` 는 루프가 생긴다:

```text
scope_int:                         scope_nt:
	push	{lr}                       push	{r4, r5, lr}
	sub	sp, sp, #28                sub	sp, sp, #28
	movs	r3, #0                     movs	r3, #0
	add	r0, sp, #4                 add	r0, sp, #4
	str	r3, [sp, #20]              str	r3, [sp, #20]
	bl	_Z4sinkPv                  bl	_Z4sinkPv
	add	sp, sp, #28                ldr	r5, [sp, #20]
	ldr	pc, [sp], #4               cbz	r5, .L40
                                   add	r4, sp, #4
                                   add	r5, r4, r5, lsl #2
                                   cmp	r4, r5
                                   beq	.L40
                               .L42:
                                   mov	r0, r4
                                   adds	r4, r4, #4
                                   bl	_ZN2NTD1Ev
                                   cmp	r5, r4
                                   bne	.L42
                               .L40:
                                   add	sp, sp, #28
                                   pop	{r4, r5, pc}
```

`str r3, [sp, #20]` 가 `size_ = 0` 이다. 저장소 16바이트는 건드리지 않는다.

defaulted 복사의 대가도 알아 둔다. `T` 가 trivial 이면 복사가 **`N` 칸 전체**를 복사한다(원소가 3개여도). 미초기화 바이트를 `std::byte` 로 복사하는 것은 허용되지만([basic.indet]), `static_vector<uint8_t, 4096>` 을 값으로 넘기면 4KB 를 다 옮긴다. `is_trivially_copyable` 을 얻는 대가다.

**용량 초과 정책.**

| 상황 | 성격 | 처리 |
|---|---|---|
| `push_back` 이 꽉 참 | 부하에 따라 일어나는 런타임 상황 | `Overflow` 정책 (`ReturnOnFull` 또는 `TrapOnFull`) |
| 빈 벡터 `pop_back`, `erase(end())`, 범위 밖 `[]` | 호출자 버그 | `assert` (릴리스는 `NDEBUG` 로 제거) |

정책을 템플릿 인자로 둔 대가는 `static_vector<int,4,A>` 와 `<int,4,B>` 가 다른 타입이라는 것이다. 표준 `push_back` 처럼 예외(`bad_alloc`)로 던질 수 없는 `-fno-exceptions` 환경이라 반환값이 기본이고, `[[nodiscard]]` 로 무시를 경고한다. `TrapOnFull` 사용자는 절대 실패하지 않으므로 `(void)` 를 써야 하는 소음이 있다 — 의도된 트레이드오프다.

**예외 안전성은 N/A.** 예외가 없는 빌드이므로 basic/strong guarantee 논의 자체가 해당되지 않는다. 이 구현을 예외 켠 호스트에서 던지는 `T` 로 쓰는 것은 안전하지 않다(`assign_range_` 중간에 던지면 크기가 어긋난다). `-fno-exceptions` 가 전제다.

**컴파일 에러로 막기.** 하네스의 `requires` 식 + trait 로 증명했고, `demo_errors.cpp` 의 `-DSHOW_ERROR` 실제 에러(trim)는 이렇다:

```text
demo_errors.cpp:17:10: error: no matching constructor for initialization of 'static_vector<MoveOnly, 4>'
./demo_static_vector.hpp:68:57: note: because 'std::is_copy_constructible_v<MoveOnly>' evaluated to false
demo_errors.cpp:19:13: error: no matching member function for call to 'push_back'
./demo_static_vector.hpp:146:55: note: because 'std::is_copy_constructible_v<MoveOnly>' evaluated to false
demo_errors.cpp:20:7: error: no matching member function for call to 'erase'
./demo_static_vector.hpp:184:49: note: because 'std::is_move_assignable_v<ConstMember>' evaluated to false
demo_errors.cpp:21:27: error: no viable conversion from 'static_vector<[...], 4>' to 'static_vector<[...], 8>'
```

`std::vector` 라면 같은 잘못이 라이브러리 내부 깊은 곳에서 터지지만, 제약을 선언에 달아 **호출 지점에서** 끝난다.

### Part C — 라이브러리 알고리즘과 Lease

세 함수 모두 몇 줄이다. `std::upper_bound` 로 같은 id 의 맨 뒤 위치를 찾으면 FIFO 가 유지되고, `expire` 는 erase-remove 관용구다(`std::remove_if` 는 지울 원소를 앞으로 대입하고 뒤쪽 moved-from 꼬리만 남긴다).

흥미로운 부분은 `Lease` 의 이동 대입이 **옛 버퍼를 반납**한다는 점이다. `erase` 가 앞으로 밀면서 대입할 때마다 덮이는 칸의 버퍼가 반납되고, 마지막 칸의 moved-from `Lease` 를 소멸해도 아무 일이 없다. 한 번의 erase 에 정확히 한 번 반납된다. 수명 처리를 하나라도 틀리면 `free_count()` 나 `bad_release` 가 어긋난다.

`enqueue` 실패 시 호출자의 프레임이 그대로라는 것도 중요하다. "넣다 실패했는데 버퍼가 사라졌다"는 펌웨어 현장 버그의 전형이다. `q_.full()` 을 먼저 보고 `insert` 까지 가지 않으니 인자는 이동되지 않는다. 시간 비교 `static_cast<int32_t>(now - deadline) >= 0` 은 32비트 tick 이 넘어가도 맞다(C 트랙의 wrap-around 비교 참고).

한계도 명시한다. `size_` 가 원자적이지 않고 `erase` 가 원소를 옮기는 동안 일관성이 깨지므로, 이 큐는 **ISR 과 공유하려면 임계 구역이 필요**하다.

### 측정 — Cortex-M 에서 정말 공짜인가

재현 명령은 맥에서 `bash asm_target.sh asm|sections|gcc|link` (`ARM_INC` 로 헤더 위치 지정). 아래는 `-O2 -DNDEBUG -fno-exceptions -fno-rtti`, `-mcpu=cortex-m4` 의 실제 결과다. 도구: Apple clang 21 (thumbv7em), `arm-none-eabi-g++` 14.2.1.

**명령어 수 비교** (`-S` 출력의 명령 줄 수, 코드 크기나 사이클이 아니다). `cbuf_*` 는 `struct { int a[8]; unsigned n; }` 를 손으로 조작한 C 스타일이다.

| 함수 | clang: C / static_vector | gcc: C / static_vector |
|---|---|---|
| push | 10 / 10 | 11 / 13 |
| erase(i) | 37 / 20 | 16 / 30 |
| sum (range-for) | 46 / 40 | 10 / 13 |

**"완전히 같다"는 결론은 틀리다.** push 는 clang 에서 동일하지만 gcc 는 2줄 많다. erase 는 양쪽 다 모양이 다르다: `std::move` 가 trivial 타입에서 `memmove` 로 내려가 호출(`__aeabi_memmove4` / `memmove`)이 생기고, 그래서 clang 은 C 루프를 펼친 37줄보다 짧고(20), gcc 는 C 루프 16줄보다 길다(30, `data()` 의 `size_ ? launder : slot` 분기가 `cbz` 로 남는다 — clang 은 이것을 접었다). 원인을 실측으로 확인했다: 복사본에서 `data()` 를 무조건 `std::launder(slot_(0))` 로 바꾸면 gcc 의 `sv_erase` 는 **20줄**로 줄어든다. 그래도 이렇게 고치지는 않았다. 비어 있을 때 launder 를 부르는 것은 UB 이기 때문이다. 정말 줄여야 한다면 `erase` 처럼 비어 있지 않음이 전제인 경로에서만 `std::launder(slot_(0))` 를 직접 쓰면 된다. **"erase 는 memmove 호출이 된다"가 사실에 가까운 요약**이다. 스택에서의 생성은 `size_` 하나만 쓴다는 것, trivial 타입에 소멸 코드가 없다는 것은 위 출력이 보여 주는 대로 사실이다.

**전역 초기화** (`.init_array` = 부팅 시 동적 초기화 코드가 있다는 뜻, CPP-B09 참고):

| 전역 선언 | clang -O2 | clang -O0 | gcc -O2 |
|---|---|---|---|
| `static_vector<int,8> g;` | `.init_array` 없음 | **있음** | **있음** |
| `constinit static_vector<int,8> g(zero_init);` | 없음 | 없음 | 없음 |
| `static_vector<NT,4> g;` (소멸자 있음) | 있음 + `__cxa_atexit` | 있음 + `__cxa_atexit` | 있음 + `__aeabi_atexit` |

기본 생성자 전역이 `.bss` 만으로 끝나는 것은 **최적화기가 접어 준 결과이지 보장이 아니다.** 같은 코드가 gcc 에서는 `.init_array` 로 남는다. 부팅 중 `.init_array` 가 돌기 전에 그 벡터를 쓰는 코드(다른 전역의 생성자, 이미 켜진 ISR)가 있으면 나중에 `size_ = 0` 이 다시 돌아 **데이터를 지운다**. 초기화 순서 문제다(CPP-B09 참고). 그래서 전역용으로 `constexpr explicit static_vector(zero_init_t)` 를 두었다. 기본 생성자에 `constexpr` 만 붙여서는 안 된다. 최소 재현 클래스로 확인한 clang 의 에러:

```text
p.cpp:13:21: error: variable does not have a constant initializer
p.cpp:13:21: note: subobject 's_' is not initialized
```

그렇다고 기본 생성자가 저장소를 0 으로 채우면 스택에 만들 때마다 N칸 memset 이 든다. 태그 생성자로 분리한 이유다. `constinit` 가 붙은 줄이 표에서 세 도구 모두 "없음"이라는 것이 그 효과다.

`T` 에 소멸자가 있는 전역은 조건부 trivial 과 무관하게 소멸 등록(`__cxa_atexit` / `__aeabi_atexit`)이 든다. 펌웨어는 대개 `main` 에서 반환하지 않으므로 이 코드는 대부분 낭비다(CPP-B09 참고).

**크기 측정** (`arm-none-eabi-size`, 실제 링크 ELF):

| 빌드 | text | data | bss |
|---|---|---|---|
| `ReturnOnFull`, `-DNDEBUG`, N 1종 | 644 | 4 | 4 |
| `BkptOnFull`(BKPT + 루프), `-DNDEBUG` | 636 | 4 | 4 |
| `TrapOnFull`(`std::abort`), `-DNDEBUG` | 5408 | 1356 | 380 |
| `ReturnOnFull`, **assert 켜짐**, N 1종 | 13040 | 1360 | 380 |
| `ReturnOnFull`, `-DNDEBUG`, **N 3종** | 1316 | 4 | 4 |

```text
--- heap / assert symbols (nm)
kinds1_ndebug
kinds1_assert         __assert_func _malloc_r _vfiprintf_r abort fiprintf
kinds1_trap_ndebug    _malloc_r abort
kinds1_bkpt_ndebug
```

세 가지가 읽힌다.

1. `ReturnOnFull` + `NDEBUG` 빌드에는 `malloc` / `operator new` 심볼이 **없다**. 힙 없는 컨테이너라는 주장이 링크 결과로 확인된다.
2. newlib 에서 `assert` 를 켜면 `__assert_func` 가 `fiprintf` 를 끌고 와 text 가 약 **20배**(644 → 13040)가 되고, `std::abort` 만으로도 text 가 약 8배(5408)다. 제품 펌웨어는 `assert` / `abort` 대신 BKPT 나 자체 fault handler 로 보낸다. BKPT 정책은 ReturnOnFull 과 거의 같다(636). 왜 `abort` 경로에 `_malloc_r` 이 끌려오는지는 콜 그래프를 따라가 보지 않았다 (미확인).
3. N 이 타입의 일부라 N 마다 코드가 복제된다. `nm` 에서 `work<4u>`, `work<8u>`, `work<16u>` 가 각각 0x14c(332)바이트로 나왔다(N 3종 = 1316 vs 1종 = 644). 대응은 ETL 처럼 **N 을 뺀 비템플릿-N 베이스(`ivector<T>`)**에 로직을 몰거나, 처리 함수를 `std::span<T>` 로 받는 것이다.

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution
# 220/220 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer solution.cpp -o solution_san && ./solution_san
# 220/220 passed, ASan/UBSan 보고 없음
```

마지막 무작위 차분 테스트는 고정 seed(`0x1234ABCD`) xorshift32 로 20000번 연산을 `std::vector<int>` 기준 모델과 매 단계 비교한다. 실제 분포: `push=6684 pop=3316 erase=3284 insert=3337 erase_range=3379`, 최종 크기 6, 불일치 0, `Tracked` 의 `bad` 0.

`demo_static_vector.hpp` 는 solution.cpp 의 컨테이너와 같은 코드다(POSIX 하네스를 데모로 끌고 오지 않으려고 분리).

## 7. 자주 하는 실수

**1. 지울 칸을 먼저 소멸하고 밀기.** `demo_bad_erase.cpp` 실제 출력 (의도적인 UB 코드, 이 환경에서 관찰한 값):

```text
(1) destroy(pos) 후 대입, 끝날 때      live=2 bad=1
(1) 스코프 종료 후                     live=0 bad=2
```

`bad=1` 은 죽은 칸에 대입, `bad=2` 는 스코프 끝의 이중 소멸이다.

```cpp
std::destroy_at(p);                 // ✗ p 칸은 이제 raw
std::move(p + 1, end(), p);         // ✗ raw 칸에 대입
// 고침: std::move(p + 1, end(), p); std::destroy_at(end() - 1); --size_;
```

**2. 끝 칸 소멸을 빠뜨리기.** 크기만 줄이면 소멸자가 영원히 안 돈다. 실제 출력:

```text
(2) 끝 칸 소멸 누락, erase 직후 (size 3) live=4 bad=0
(2) 스코프 종료 후 (0 이어야 정상) live=1 bad=0
```

`Lease` 라면 그대로 버퍼 누수다.

**3. 비-trivial 타입을 `memmove` 로 밀기.** 자기 자신을 가리키는 포인터가 있는 타입에서:

```text
(3) memmove erase: d[0].v=2  *d[0].self=3  self==&v? NO
    correct erase: w[0].v=2  *w[0].self=2  self==&v? yes
```

`std::move` 는 trivially copyable 일 때 알아서 memmove 로 내려간다(측정 절 참고). 직접 `memmove` 를 쓸 이유가 없다.

**4. 임시 객체 없이 삽입.** 실제 출력:

```text
(4) 임시 없는 insert(begin, v[2]): {2, 1, 2, 3} (기대 {3, 1, 2, 3})
```

밀고 나면 `v[2]` 참조가 가리키던 칸의 값이 바뀐다. 고침은 밀기 전에 `T tmp(args...)`.

**5. 꽉 찼는지 임시 객체 다음에 확인.** 실패할 호출이 임시 객체(생성자·소멸자 왕복)를 만들게 되고, `T&&` 인자라면 이미 이동해 버려 호출자의 객체가 비어 버린다. 검사를 먼저 해야 "실패 시 인자 불변"이 성립한다.

**6. `size_` 를 생성 전에 올리기.** `++size_` 를 `construct_at` 앞에 두면 그 사이에 `T` 의 생성자가 같은 벡터를 들여다보거나 디버거가 멈췄을 때 `[0, size)` 안에 raw 칸이 보인다. 불변식은 생성이 끝난 뒤에 갱신한다.

**7. 기본 생성자에서 저장소를 0 으로 채우기 / `= default` 로 두기.** `static_vector() = default;` 면 `static_vector<...> v{};` 가 값 초기화라서 저장소 전체를 0 으로 채운다([dcl.init]: user-provided 가 아닌 기본 생성자 → zero-initialize). 그래서 `static_vector() noexcept {}` 로 user-provided 를 만들었다.

**8. 제품 빌드에 `assert` 를 남겨 두기.** newlib 환경에서 13KB 다(측정 절). 게다가 `-DNDEBUG` 빌드에서는 **검사가 사라지므로** 용량 초과처럼 런타임에 일어날 수 있는 상황을 `assert` 로만 처리하면 제품에서 메모리를 덮어쓴다. 그래서 용량 초과는 정책, 전제조건만 assert.

## 8. 면접 Q&A

<details>
<summary>Q1. 왜 그냥 <code>std::array&lt;T, N&gt;</code> + 크기 변수로 만들면 안 되나요?</summary>

`std::array` 는 N개를 전부 생성하고 끝까지 살려 둔다. 기본 생성자가 없는 `T` 는 못 담고, 쓰지 않는 칸에도 생성자·소멸자 비용이 들며, `pop` 해도 객체는 죽지 않는다(`Lease` 라면 버퍼가 반납되지 않음). 하네스의 비교: `std::array<Tracked, 3>` 는 생성 3회, 빈 `static_vector<Tracked, 4>` 는 0회. **꼬리질문:** 저장 공간은 같은데 왜 `union` 이나 `std::byte` 를 쓰나 — 객체 생성 없이 공간과 정렬만 확보하기 위해서다. `T` 가 trivial 타입에 한정된다면 배열 + 크기로 충분하고, 그게 더 단순한 선택이다.

</details>

<details>
<summary>Q2. <code>erase</code> 에서 왜 지울 원소가 아니라 맨 끝 원소를 소멸시키나요?</summary>

대입으로 뒤 원소를 당기면 지울 값은 덮이고 맨 끝 칸이 moved-from 으로 남기 때문이다. 살아 있는 칸은 `[0, size)` 라는 불변식을 유지하려면 소멸은 끝 칸이어야 한다. 표준 `std::vector` / `inplace_vector` 의 복잡도 조항도 "대입 = 뒤 원소 수, 소멸 = 지운 수"다. **꼬리질문:** `T` 가 대입 불가면? — 그런 `T` 로 `erase` 는 이 구현에서 컴파일되지 않는다. 필요하면 소멸 후 재생성 경로를 따로 만들어야 하지만, 대입 불가 타입의 중간 삭제는 드물다.

</details>

<details>
<summary>Q3. <code>v.insert(v.begin(), v[2])</code> 는 안전한가요? <code>v.push_back(v[0])</code> 은요?</summary>

`push_back(v[0])` 은 이 컨테이너에선 항상 안전하다(재할당 없음, 새 칸은 raw, 인자는 살아 있는 다른 칸). `insert` 는 구현이 보장해야 한다. 밀기 전에 `T tmp(args...)` 를 만들지 않으면 인자 참조가 민 뒤의 값을 보게 된다(실제로 `{2, 1, 2, 3}` 이 나왔다). 비용은 임시 1회 생성·소멸이고, `pos == end` 라면 밀 게 없어 임시도 필요 없다.

</details>

<details>
<summary>Q4. 이동이 왜 O(n) 인가요? 이동 후 원본은 어떤 상태인가요?</summary>

인라인 저장소에는 훔칠 포인터가 없어서 원소별로 이동 생성해야 한다(CPP-M09). 이 구현의 원본은 크기가 유지되고 원소만 moved-from 이다. `std::array` 와 같고, trivial 타입의 defaulted 이동과도 일치한다. 표준 `std::vector` 의 "비워지는" 이동과 다르다. **꼬리질문:** 이동 후 반복자는 — 원본 칸을 계속 가리키며, 새 벡터의 원소를 가리키지 않는다. 이동으로 무효화되지 않고 버퍼가 넘어가는 `std::vector` 와 다른 점이다.

</details>

<details>
<summary>Q5. 예외 없이 용량 초과를 어떻게 처리하나요? assert, 반환값, abort 중 무엇을 고르겠습니까?</summary>

상황으로 나눈다. 부하에 따라 실제로 일어나는 일(큐가 꽉 참)은 **반환값으로 처리**하고 호출자가 드롭·재시도 정책을 정한다. 일어나면 안 되는 설계 불변식이면 **즉시 멈춘다.** 호출자 버그는 `assert`. 다만 `assert` / `std::abort` 를 newlib 에 그대로 쓰면 이 레슨의 실측으로 text 가 644 → 13040 / 5408 바이트다. 제품에서는 BKPT 나 fault handler 로 보낸다(636 바이트). 실패 시 인자를 건드리지 말아야 재시도가 가능하다는 것도 말해야 한다.

</details>

<details>
<summary>Q6. 조건부 trivial 소멸자는 왜 필요하고, C++17 이전엔 어떻게 했나요?</summary>

`static_vector<int, N>` 이 trivial 타입이면 전역 소멸 등록이 사라지고(`.init_array` 표 참고), trivially copyable 이 되어 `memcpy` 가 합법이 되며, 소멸 코드가 전혀 없다. C++20 은 P0848 로 `~X() requires ... = default;` 를 허용한다. 이전에는 `std::conditional` 또는 부분 특수화한 **베이스 클래스 두 개**(trivial 소멸 버전과 비-trivial 버전)를 상속해서 같은 효과를 냈다 — `std::optional` 구현들이 그렇게 만들어졌다. **꼬리질문:** 이렇게 하면 무조건 이득인가 — `T` 가 trivial 이면 복사가 `N` 칸 전체 비트 복사가 되어 원소가 적을 때 오히려 낭비다.

</details>

<details>
<summary>Q7. C++26 <code>std::inplace_vector</code> 나 ETL 을 쓰면 되지 않나요? 무엇이 다른가요?</summary>

`std::inplace_vector` (C++26, `__cpp_lib_inplace_vector`)는 표준 구현이다. `push_back` 은 용량을 넘으면 `std::bad_alloc` 을 던지고, 예외 없이 쓰려면 `try_push_back` / `try_emplace_back` 을 쓴다. 현재 표준 초안에서 이들은 실패 시 `nullopt` 를 돌려주며, 처음 채택된 안(`202406L`)의 반환 타입은 달랐다고 cppreference 가 기록한다(`202603L` 에서 변경). 또 `unchecked_*` 버전은 `size() < capacity()` 가 전제조건이다. `-fno-exceptions` 펌웨어에서 쓰려면 `bad_alloc` 경로를 어떻게 처리할지 툴체인을 확인해야 한다. 이 레슨의 도구로 확인한 바로는 맥 SDK(MacOSX26.4)의 libc++ 와 arm-none-eabi 14.2.1 의 libstdc++ 둘 다 `<inplace_vector>` 헤더 파일 자체가 없다. 지금 이 툴체인에서는 못 쓴다.

ETL 의 `etl::vector<T, N>` 은 `etl::ivector<T>` 를 상속해 N 없는 베이스로 쓸 수 있다(헤더 주석: "Can be used as a reference type for all vectors containing a specific type"). `push_back` 의 용량 검사는 `ETL_CHECK_PUSH_POP` 를 정의해야 켜진다(`error_handler.h`: 정의 안 하면 매크로가 빈 정의). **기본 빌드에서 ETL `push_back` 은 검사하지 않는다**는 뜻이므로, 쓸 때 설정을 확인해야 한다.

</details>

<details>
<summary>Q8. ISR 과 메인이 같은 static_vector 를 쓰면 어떻게 되나요?</summary>

안전하지 않다. `size_` 갱신과 원소 생성이 비원자적이고, `erase` 는 원소를 옮기는 동안 중간 상태를 노출한다. 임계 구역으로 감싸거나, 끝쪽 삽입/앞쪽 제거만 있는 구조라면 원형 큐(SPSC) 쪽이 맞다. 그리고 `.init_array` 전에 ISR 이 쓸 수 있는 전역이면 `constinit` 생성자를 써야 한다는 점이 함께 나온다.

</details>

<details>
<summary>Q9. ASan 으로 테스트하니 pop_back 한 칸을 읽는 버그가 안 잡힙니다. 왜죠?</summary>

저장소가 객체 하나(벡터) 안의 평범한 바이트 배열이라 ASan 입장에선 유효한 메모리다. `demo_asan_pop.cpp` 실제 결과: `plain` 모드는 `read popped slot = 22` 로 조용했고, 빈 칸을 `ASAN_POISON_MEMORY_REGION` 으로 표시한 `poisoned` 모드는 `use-after-poison`, libc++ `std::vector` 는 `container-overflow` 로 잡혔다. 해결은 `pop_back`/`erase` 에서 빈 칸을 poison 하는 것(디버그 빌드 전용)이다.

</details>

## 9. 추가 연습

1. **`resize(n)` / `resize(n, value)`** — 줄이면 소멸, 늘리면 값 초기화(또는 `value` 복사). `N` 을 넘으면 `N` 까지만 채우고 `false`.

   <details><summary>답</summary>

   `truncate(count)` 를 먼저 호출하고(줄이는 경우), 부족분을 `min(count, N)` 까지 `emplace_back` 한다. `emplace_back()` 은 `std::construct_at(p)` 로 **값 초기화**이므로 `int` 는 0 이다. 쓰레기 값이 아니다.

   </details>

2. **`swap_remove(i)`** — 순서를 포기한 O(1) 삭제. 함정: 마지막 칸을 지울 때 자기 자신으로의 이동 대입을 피해야 한다.

   <details><summary>답</summary>

   `if (i != size_ - 1) d[i] = std::move(d[size_ - 1]);` 후 끝 칸 `destroy_at`, `--size_`. 원소 수와 무관하게 이동 1회(표 끝 원소 지우기는 0회). 하네스의 `T1::self_move == 0` 이 자기 이동 대입이 없음을 확인한다.

   </details>

3. **작은 `size_type`** — `N` 을 담는 가장 작은 부호 없는 정수로 `size_` 를 바꿔라.

   <details><summary>답</summary>

   `std::conditional_t<(N <= 0xFF), uint8_t, ...>` 체인. 실측: `sizeof(sv<uint8_t, 16>) == 17`, `sizeof(sv<uint8_t, 256>) == 258`. 그러나 `sizeof(sv<uint32_t, 16>) == 68` — `T` 정렬 때문에 뒤에 패딩이 붙어 절약이 없다. 또 경계에서 `uint8_t` 는 255 다음에 0 으로 돌아가므로 `size_ == N` 검사를 정확히 해야 한다(`N = 255` 에서 `emplace_back` 300회 → `size 255`).

   </details>

4. **`erase_if(v, pred)`** — C++20 `std::erase_if` 모양의 자유 함수, 지운 개수 반환.

   <details><summary>답</summary>

   `std::remove_if` 후 꼬리 소멸. 본문 `erase(first, last)` 또는 `truncate` 로 처리. 지울 게 없을 때(0)와 전부 지울 때(`live == 0`)를 표에 넣어라.

   </details>

5. **작은 용량에서 변환** — `static_vector<T, M>` (`M < N`) 에서 `static_vector<T, N>` 으로의 변환 생성자. 넘칠 수 있는 방향은 컴파일 불가.

   <details><summary>답</summary>

   `template <std::size_t M> requires(M < N && ...) explicit sv(const sv<T, M>& o)`. `M == N` 은 복사 생성자의 몫이라 제외, `explicit` 으로 O(n) 복사가 암시적으로 숨지 않게 한다. `is_constructible_v<sv<int,4>, const sv<int,8>&>` 가 false 인 것으로 증명.

   </details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
# 29/29 passed (sanitizer 빌드도 보고 없음)
```

## 10. 핵심 정리

- `static_vector` = `alignas(T) std::byte` 저장소 + `size_`. 불변식은 "`[0, size)` 만 살아 있다" 하나다.
- raw 칸에는 **생성**, 살아 있는 칸에는 **대입**, 남는 칸은 **소멸**. `erase` 는 밀고 끝 칸 소멸, `insert` 는 끝 칸 생성 + 대입.
- 별칭 삽입은 임시 객체로 막는다. 꽉 찼는지는 임시 객체보다 **먼저** 검사한다. 실패한 호출은 인자를 건드리지 않는다.
- 이동도 원소별 O(n), 원본 크기 유지. `T` 가 trivial 이면 C++20 `requires ... = default` 로 벡터도 trivial — `T` 가 비-trivial 이면 소멸 루프가 생기고, 전역에는 소멸 등록이 든다.
- 전역은 `constinit` + 태그 생성자로 `.init_array` 를 없앤다. 기본 생성자 전역은 clang 은 접고 gcc 는 동적 초기화로 남긴다 — 보장이 아니다.
- Cortex-M 실측: `ReturnOnFull` 644B, `assert` 켜면 13040B, `std::abort` 5408B, BKPT 636B. N 마다 코드가 복제되니 `std::span` 이나 N 없는 베이스로 처리한다. erase 는 C 루프와 같은 코드가 아니라 `memmove` 호출이다.
- ISR 과 공유는 임계 구역, ASan 은 빈 칸을 poison 해야 잡는다. 예외 안전성은 `-fno-exceptions` 라 N/A.

## 참고자료

- [cppreference — std::inplace_vector](https://en.cppreference.com/w/cpp/container/inplace_vector) · C++ draft: [inplace.vector.overview](https://eel.is/c++draft/inplace.vector.overview), [inplace.vector.modifiers](https://eel.is/c++draft/inplace.vector.modifiers)
- [cppreference — std::launder](https://en.cppreference.com/w/cpp/utility/launder) · [construct_at](https://en.cppreference.com/w/cpp/memory/construct_at) · [destroy_at](https://en.cppreference.com/w/cpp/memory/destroy_at)
- [CWG 2182 — Pointer arithmetic in array-like containers](https://cplusplus.github.io/CWG/issues/2182.html) (상태: drafting)
- C++20 표준: [class.dtor] prospective destructor, [special] eligible special member, [ptr.launder], [intro.object] implicit object creation, [basic.indet], [basic.lval], [dcl.init] (value-init)
- P0848 Conditionally Trivial Special Member Functions · P0843 `inplace_vector`
- ETL: [include/etl/vector.h](https://github.com/ETLCPP/etl/blob/master/include/etl/vector.h) (`ivector<T>`, `vector<T, N>`) · [include/etl/error_handler.h](https://github.com/ETLCPP/etl/blob/master/include/etl/error_handler.h) (`ETL_CHECK_PUSH_POP`)
- 레슨 내 데모: `demo_errors.cpp`, `demo_bad_erase.cpp`, `demo_asan_pop.cpp`, `demo_target.cpp`, `demo_link.cpp`, `asm_target.sh` (Cortex-M 측정, Lima VM `elinux` 필요)
