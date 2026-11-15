---
id: CPP-M09
time: 75분
updated: 2026-11-08
---
# CPP-M09 move semantics의 비용: 고정 용량 버퍼에선 공짜가 아니다

## 학습 목표

- 복사/이동 횟수를 세는 **계측 타입(instrumented type)** 으로 "포인터를 훔치는 이동"과 "요소를 하나씩 옮기는 이동"을 숫자로 구분한다.
- `std::array` 멤버의 이동 비용이 **사용 중인 개수가 아니라 용량(capacity)** 에 비례한다는 것과, trivially copyable 요소에서는 이동과 복사가 **같은 기계어**라는 것을 어셈블리로 확인한다.
- `noexcept` 가 `std::move_if_noexcept` 와 재배치(relocation)를 어떻게 바꾸는지 설명하고, **`-fno-exceptions` 에서는 라이브러리마다 결과가 다르다**는 것을 직접 돌려 본다.
- 소멸자 하나를 선언했을 때 이동이 조용히 복사로 바뀌는 함정과, 이동당한 인라인 객체가 "여전히 유효해 보이는" 함정을 잡는다.
- 큰 프레임을 큐로 넘길 때 값 전달 대신 **풀 + 1바이트 이동 전용 핸들**로 설계를 바꾸고, 그 이득을 바이트 수로 말한다.

## 1. 문제

펌웨어 면접에서 "move 쓰면 빠르지 않나요?"는 함정 질문이다. 힙이 없는 펌웨어의 객체는 버퍼를 멤버로 품고 있어서, `std::move` 를 붙여도 256바이트는 256바이트만큼 옮겨야 한다. 이동이 싼 경우와 그렇지 않은 경우를 구분해야 ISR 지연과 스택 사용량을 예측할 수 있다.

이동 생성자 작성법 자체는 MCPP-B03 이 담당한다. 이 레슨은 **비용**만 본다. 이동이 싼 이유는 "이동"이라는 문법이 아니라, 객체가 데이터를 **간접 참조(포인터)로 갖고 있기 때문**이다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">같은 std::move, 다른 비용</text>

  <text x="10" y="50" fill="currentColor" font-weight="bold">std::vector 멤버: 포인터 3개만 옮긴다</text>
  <rect x="10" y="62" width="150" height="34" class="f-mut" stroke="currentColor"/>
  <text x="20" y="84" fill="currentColor" font-family="monospace" font-size="12">src: null,null,null</text>
  <rect x="10" y="112" width="150" height="34" class="f-ok" stroke="currentColor"/>
  <text x="20" y="134" fill="currentColor" font-family="monospace" font-size="12">dst: ptr,size,cap</text>
  <rect x="300" y="86" width="400" height="36" class="f-acc" stroke="currentColor"/>
  <text x="314" y="109" fill="currentColor" font-family="monospace" font-size="12">힙 버퍼 [0][1][2] ... [n-1]   (주소 그대로)</text>
  <path d="M160 129 L300 110" stroke="currentColor" fill="none"/>
  <text x="178" y="146" fill="currentColor">요소 복사 0, 이동 0</text>

  <text x="10" y="186" fill="currentColor" font-weight="bold">std::array 멤버: 버퍼가 객체 안에 있다</text>
  <rect x="10" y="198" width="340" height="36" class="f-mut" stroke="currentColor"/>
  <text x="20" y="221" fill="currentColor" font-family="monospace" font-size="12">src: [0][1][2] ... [N-1]  count</text>
  <rect x="10" y="266" width="340" height="36" class="f-ok" stroke="currentColor"/>
  <text x="20" y="289" fill="currentColor" font-family="monospace" font-size="12">dst: [0][1][2] ... [N-1]  count</text>
  <path d="M40 234 L40 266 M90 234 L90 266 M140 234 L140 266 M220 234 L220 266" stroke="currentColor" fill="none"/>
  <text x="372" y="222" fill="currentColor">요소마다 이동 생성자 1번 = N번</text>
  <text x="372" y="244" fill="currentColor">쓰지 않는 칸까지 전부 옮긴다</text>
  <text x="372" y="266" fill="currentColor">trivial 요소면 memcpy(복사와 동일)</text>
  <text x="372" y="288" fill="currentColor">데이터 주소가 바뀐다</text>
  <text x="10" y="322" fill="currentColor">핵심: 이동 비용 = 객체가 "직접 품은" 바이트 수. 간접 참조가 있어야 훔칠 게 생긴다.</text>
</svg>
```

### Part A — 계측 타입과 두 소유 방식의 비교

- `ProbeT<NX>` 의 복사/이동 생성·대입과 소멸자가 전역 `g_ops` 를 올리게 채운다. 이동당한 쪽의 `value` 는 `MOVED_FROM(-1)` 으로 바꾼다.
- `measure_move(Owner& src)` 를 구현한다. `Owner dst(std::move(src))` 동안의 요소 복사·이동 횟수, 버퍼 주소가 유지됐는지(`buffer_stolen`), 이동 후 `src.size()` 를 돌려준다.
- `VecOwner`(`std::vector<Probe>`)와 `InlineOwner<N>`(`std::array<Probe,N>` + `count`)를 비교한다. 둘 다 Rule of Zero 이고 이동 연산은 컴파일러가 만든다.
- 하네스는 `InlineOwner<64>` 를 `used = 0, 1, 8, 64` 로 만들어 **항상 64번** 이동하는지 본다. `vector` 는 호스트 전용 실험이다(펌웨어 금지 이유는 CPP-B14).

### Part B — noexcept 와 재배치

- `reloc_choice<T>()`: `std::move_if_noexcept` 가 `T&&` 를 주는지 `const T&` 를 주는지 컴파일 타임에 답한다.
- `relocate(from, n, to)`: vector 재배치의 미니 모델. `std::construct_at` + `std::move_if_noexcept` 로 옮기고 원본을 파괴한다.
- `vector_growth_cost<T>(n)`: 실제 `std::vector` 를 `reserve(2n)` 으로 재배치시켜 비용을 잰다. **기댓값이 라이브러리와 `-fno-exceptions` 여부에 따라 다르다.** 왜 다른지 5절에서 실측으로 확인한다.
- `DtorKeepsMove`: 소멸자를 유지하면서 이동을 되살린다. 옆의 `DtorBlocksMove` 는 소멸자 선언 때문에 이동이 복사로 바뀐 사례다.

### Part C — 256바이트 프레임을 큐로 넘기기

DMA ISR → 파서 → 앱으로 프레임이 3단계(hop)를 지나는 상황이다.

- `ExchangeOnMove<T>`: 이동당하면 스스로 `T{}` 로 돌아가는 멤버. `TxDesc` 는 이것 덕분에 Rule of Zero 를 유지하면서 "이동당한 디스크립터의 길이는 0"을 보장한다.
- `FrameHandle`: 풀 슬롯 번호 1바이트를 들고 다니는 이동 전용 RAII 핸들. 이동 시 원본 무효화, 이동 대입 시 자기 슬롯 먼저 반납, 자기 대입 안전, 소멸 시 반납.
- `route<T>(item, hops)`: 같은 코드로 `T = Frame` 과 `T = FrameHandle` 을 돌린다. 비용 차이가 오직 `T` 에서만 나오게 하기 위해서다.

## 2. Problem (English)

> **The cost of moving fixed-capacity objects.**
>
> Firmware objects usually embed their buffers. Show, by measurement, when a C++ move is a pointer steal and when it is an element-wise transfer, then design around it.
>
> 1. Implement `template <bool NX> struct ProbeT` whose copy/move constructors, copy/move assignments and destructor increment global counters. The move operations are `noexcept(NX)` and mark the source as moved-from.
> 2. Implement `template <class Owner> MoveReport measure_move(Owner& src)`, reporting element copies and moves during `Owner dst(std::move(src))`, whether the data pointer was preserved, and `src.size()` afterwards. Compare `struct VecOwner { std::vector<Probe> items; }` with `template <size_t N> struct InlineOwner { std::array<Probe, N> items; size_t count; }`.
> 3. Implement `template <class T> constexpr Reloc reloc_choice()` and `template <class T> size_t relocate(T* from, size_t n, T* to)` using `std::move_if_noexcept`. Measure a real `std::vector` reallocation. State what changes under `-fno-exceptions`, per standard library, by running it.
> 4. Implement `ExchangeOnMove<T>` (a member that resets its source on move) and `class FrameHandle`, a move-only one-byte RAII handle to a pool slot.
> 5. Implement `template <class T> void route(T& item, unsigned hops)`, which passes `item` through a 2-slot ring `hops` times, and compare the bytes transferred for a 258-byte `Frame` against a `FrameHandle`.
>
> **Constraints:** C++20, `-fno-exceptions -fno-rtti`, no heap in Part C. `std::vector` is used only as a host-side reference point.
>
> **Example:** moving `InlineOwner<64>` holding one element performs 64 element moves. Moving `VecOwner` holding 64 elements performs 0. Three hops cost 1548 bytes by value and 6 bytes by handle.

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 대비
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
# 처음엔 "42/101 passed" (exit 1) — TODO 를 채워 101/101 을 만든다
```

## 4. 힌트

<details><summary>힌트 1 — measure_move 에서 카운터를 언제 읽나</summary>

`dst` 가 살아 있는 동안 읽어야 한다. 함수가 끝나면 `dst` 가 소멸하면서 `dtor` 가 늘어난다. 복사/이동 수만 필요하면 상관없지만, `ops_reset()` 은 반드시 **이동 직전**에 해야 한다. `src` 를 만드는 동안의 기본 생성·값 대입이 섞이면 안 된다.
</details>

<details><summary>힌트 2 — InlineOwner 의 비용이 왜 used 와 무관한가</summary>

`std::array<Probe, N>` 은 "몇 칸을 쓰는지" 모른다. `count` 는 옆에 있는 그냥 정수다. 컴파일러가 만든 이동 생성자는 멤버별로 이동하므로 N칸 전부를 이동한다.
</details>

<details><summary>힌트 3 — reloc_choice</summary>

`std::move_if_noexcept(x)` 의 **반환 타입**을 보면 된다. `decltype(std::move_if_noexcept(std::declval<T&>()))` 가 rvalue 참조면 이동, 아니면 복사다. `std::is_rvalue_reference_v` 로 판별한다.
</details>

<details><summary>힌트 4 — relocate 의 대상 메모리</summary>

`to` 쪽은 아직 객체가 없는 raw storage 다. 대입이 아니라 생성이어야 하므로 `std::construct_at(to + i, std::move_if_noexcept(from[i]))` 를 쓴다. 저장소 수명 규칙 자체는 CPP-M07 참고. 다 옮긴 뒤 `std::destroy_n(from, n)` 으로 원본을 끝낸다.
</details>

<details><summary>힌트 5 — 왜 DtorKeepsMove 에 복사까지 적어야 하나</summary>

이동 생성자를 선언하면 복사 생성자·복사 대입은 **deleted** 로 정의된다. 이동만 `= default` 하면 복사가 사라진다. 복사도 필요하면 네 개를 모두 적는다.
</details>

<details><summary>힌트 6 — FrameHandle 이동 대입의 순서</summary>

1. 자기 대입이면 아무것도 하지 않는다. 2. 내가 들고 있던 슬롯을 풀에 반납한다. 3. `std::exchange(o.idx_, kNoFrame)` 로 가져온다. 2번을 빼면 덮어쓰는 슬롯이 누수되고, 1번을 빼면 자기 슬롯을 반납한 뒤 빈 핸들을 가져오게 된다.
</details>

## 5. 풀이

### Part A: 측정 결과

`solution.cpp` 의 하네스가 검사하는 값이다 (`-fno-exceptions`, Apple clang 21 / libc++ 21, 그리고 Lima VM 의 g++ 15.2 / libstdc++ 에서도 101/101).

| 객체 | 사용 중 | 요소 복사 | 요소 이동 | 버퍼 주소 유지 | 이동 후 `src.size()` |
|---|---:|---:|---:|---|---:|
| `VecOwner` | 1 / 8 / 64 | 0 | 0 | 예 | 0 |
| `InlineOwner<64>` | 0 / 1 / 8 / 64 | 0 | **64** | 아니오 | 그대로 |
| `InlineOwner<8>` | 3 | 0 | 8 | 아니오 | 3 |
| `InlineOwner<32>` | 3 | 0 | 32 | 아니오 | 3 |

읽는 법은 세 가지다.

1. **`vector` 는 요소 수와 무관하다.** 표준도 이것을 복잡도로 못 박는다. `[container.reqmts]`: 이동 생성은 "`array` 와 `inplace_vector` 는 선형, 나머지 표준 컨테이너는 상수".
2. **`std::array` 는 용량에 비례한다.** `used = 0` 이어도 64번이다. `count` 를 보고 필요한 만큼만 옮기려면 CPP-M10 의 `static_vector` 처럼 직접 구현해야 한다.
3. **이동 후 원본 상태는 타입마다 다르다.** 위 표의 `size() == 0` 은 libc++/libstdc++ 에서 실측한 결과다. cppreference 의 vector 생성자 페이지는 이동 후 `other` 가 비어 있다고 적지 않고, 나는 표준에서 이를 직접 보장하는 문구를 확인하지 못했다(**미확인**). 요소 참조·반복자가 새 컨테이너를 가리키며 유효하다는 쪽은 `[container.reqmts]` 의 일괄 규정에 있고, 더 직접적인 문구는 LWG 2321(Open)에서 논의 중이다. 코드에서 의존하지 말고 이동한 객체는 다시 쓰기 전에 대입하자.

기계어로도 확인한다. `demo_host_asm.cpp` 를 arm64 에서 컴파일한 실제 출력이다. 이 레슨의 어셈블리 목록은 모두 실제 출력에서 지시어를 걸러낸 것이고, `;` 뒤 설명과 `...` 생략 표시는 읽기 쉽게 덧붙였다.

```bash
c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -S demo_host_asm.cpp -o -
```

```text
__Z8move_vecP3VecRS_:                   ; Vec 이동 (vector 멤버)
	stp	xzr, xzr, [x0]
	str	xzr, [x0, #16]
	ldr	q0, [x1]
	str	q0, [x0]
	ldr	x8, [x1, #16]
	str	x8, [x0, #16]
	stp	xzr, xzr, [x1]
	str	xzr, [x1, #16]
	ret
__Z8move_arrP5Arr64RS_:                 ; array<uint32_t,64> 이동
	ldp	q0, q1, [x1]
	ldp	q2, q3, [x1, #32]
	stp	q2, q3, [x0, #32]
	stp	q0, q1, [x0]
	...                                 ; 같은 4줄 묶음이 #64, #128, #192 에서 반복
	ret
```

`copy_arr` 의 출력은 `move_arr` 와 **명령이 완전히 같았다** (16개의 ldp/stp + ret). 요소가 trivially copyable 이면 이동 연산자가 할 수 있는 일이 복사뿐이다.

시간도 재 봤다. `demo_bench.cpp`, 호스트 arm64 측정값이다.

```text
bytes    vector ns/move  array ns/move
16                 0.79           1.61
64                 1.29           2.06
256                0.67           3.24
1024               0.96          13.37
4096               2.42          59.35
65536              1.40        1349.21
```

같은 프로그램을 앞서 한 번 더 돌렸을 때는 vector 칸이 1.3~2.2ns, array 4096 이 90ns, 65536 이 1578ns 였다. 절대값은 2배 가까이 흔들리므로 **경향만** 읽는다. 작은 크기에서는 차이가 거의 없고, 큰 크기에서 array 쪽만 크기에 비례해 늘어난다. 이 벤치마크는 힙 할당 비용을 포함하지 않는다(이동만 반복). 그리고 호스트 수치이므로 Cortex-M 의 사이클 수는 아니다 — Cortex-M4 사이클은 **미측정**이다.

### Part B: 재배치에서 noexcept 가 하는 일

vector 가 커질 때 새 버퍼로 요소를 옮기다 3번째에서 예외가 나면 어떻게 될까. 이미 이동한 원본은 망가졌으므로 되돌릴 수 없다. 그래서 표준 라이브러리는 **이동이 던질 수 있으면 복사**를 고른다(강한 예외 보장, strong exception guarantee). 그 선택 규칙이 `std::move_if_noexcept` 다.

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">std::move_if_noexcept 의 선택 (표준 규칙, -fno-exceptions 와 무관)</text>
  <rect x="220" y="38" width="300" height="38" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="238" y="62" fill="currentColor" font-family="monospace" font-size="12">is_nothrow_move_constructible&lt;T&gt; ?</text>
  <path d="M300 76 L170 120" stroke="currentColor" fill="none"/>
  <path d="M440 76 L560 120" stroke="currentColor" fill="none"/>
  <text x="190" y="104" fill="currentColor">예</text>
  <text x="510" y="104" fill="currentColor">아니오</text>
  <rect x="30" y="120" width="280" height="42" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="50" y="146" fill="currentColor" font-family="monospace" font-size="12">T&amp;&amp; → 이동 (강한 보장 유지)</text>
  <rect x="420" y="120" width="280" height="42" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="444" y="146" fill="currentColor" font-family="monospace" font-size="12">is_copy_constructible&lt;T&gt; ?</text>
  <path d="M480 162 L400 206" stroke="currentColor" fill="none"/>
  <path d="M640 162 L640 206" stroke="currentColor" fill="none"/>
  <text x="412" y="190" fill="currentColor">예</text>
  <text x="654" y="190" fill="currentColor">아니오</text>
  <rect x="270" y="206" width="240" height="42" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="288" y="232" fill="currentColor" font-family="monospace" font-size="12">const T&amp; → 복사</text>
  <rect x="530" y="206" width="200" height="42" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="544" y="232" fill="currentColor" font-family="monospace" font-size="12">T&amp;&amp; → 이동 (약한 보장)</text>
  <text x="10" y="272" fill="currentColor">이 함수의 결과와 "vector 가 이 함수를 실제로 쓰는가"는 별개다. 아래 표를 보자.</text>
</svg>
```

하네스의 `relocate()` 는 위 그림대로 동작한다. 그런데 **실제 `std::vector` 는 라이브러리마다 다르게 동작한다.** `bash run_matrix.sh` 의 실제 출력이다 (요소 8개, `reserve(8→16)`).

```text
library=libc++ 210106  exceptions=OFF (-fno-exceptions)
  noexcept  is_nothrow_move=1  move_if_noexcept=T&&     reserve(8->16): copies=0 moves=8
  throwing  is_nothrow_move=0  move_if_noexcept=const&  reserve(8->16): copies=0 moves=8
  push_back x64 without reserve: relocations=63 (final capacity 64)
library=libc++ 210106  exceptions=ON
  noexcept  is_nothrow_move=1  move_if_noexcept=T&&     reserve(8->16): copies=0 moves=8
  throwing  is_nothrow_move=0  move_if_noexcept=const&  reserve(8->16): copies=8 moves=0
  push_back x64 without reserve: relocations=63 (final capacity 64)
library=libstdc++ 20260321  exceptions=OFF (-fno-exceptions)
  noexcept  is_nothrow_move=1  move_if_noexcept=T&&     reserve(8->16): copies=0 moves=8
  throwing  is_nothrow_move=0  move_if_noexcept=const&  reserve(8->16): copies=8 moves=0
  push_back x64 without reserve: relocations=63 (final capacity 64)
library=libstdc++ 20260321  exceptions=ON
  noexcept  is_nothrow_move=1  move_if_noexcept=T&&     reserve(8->16): copies=0 moves=8
  throwing  is_nothrow_move=0  move_if_noexcept=const&  reserve(8->16): copies=8 moves=0
  push_back x64 without reserve: relocations=63 (final capacity 64)
```

요약하면 이렇다.

| 표준 라이브러리 | 예외 | `noexcept` 이동 | 던질 수 있는 이동 |
|---|---|---|---|
| libc++ 21 (맥) | 끔 | 이동 8 | **이동 8** |
| libc++ 21 (맥) | 켬 | 이동 8 | 복사 8 |
| libstdc++ 15 (Linux) | 끔 | 이동 8 | **복사 8** |
| libstdc++ 15 (Linux) | 켬 | 이동 8 | 복사 8 |

`noexcept` 자체는 `-fno-exceptions` 에서도 타입 시스템에 그대로 남는다(`is_nothrow_move=0`, 그리고 하네스의 `static_assert` 가 이 플래그로 컴파일된다). 차이는 라이브러리 안에 있다. 맥 SDK 의 `__memory/uninitialized_algorithms.h` 에 있는 재배치 코드다.

```cpp
#if _LIBCPP_HAS_EXCEPTIONS
      allocator_traits<_Alloc>::construct(__alloc, std::__to_address(__result), std::move_if_noexcept(*__iter));
#else
      allocator_traits<_Alloc>::construct(__alloc, std::__to_address(__result), std::move(*__iter));
#endif
```

예외가 없으면 롤백할 일도 없으니 libc++ 는 그냥 이동한다. 합리적인 최적화지만 **표준이 약속한 동작은 아니고 구현 세부**다. libstdc++ 의 `vector.tcc::reserve` 는 `_S_use_relocate()` 가 참일 때(이동이 nothrow 일 때)만 재배치하고, 아니면 `_GLIBCXX_MAKE_MOVE_IF_NOEXCEPT_ITERATOR` 로 옮긴다. g++ 15 의 `vector.tcc` 에는 `__cpp_exceptions` 분기가 없었다.

펌웨어에서 쓰는 것은 대개 후자다. Lima VM 의 `arm-none-eabi-g++ 14.2.1` 로 `demo_arm_reserve.cpp` 를 컴파일만 해서 `reserve` 안의 호출을 뽑았다. 실행은 하지 않았고, `bl` 대상이 복사냐 이동이냐만 본 것이다.

```bash
arm-none-eabi-g++ -std=c++20 -O2 -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb -DNX=false -S demo_arm_reserve.cpp -o - | arm-none-eabi-c++filt | grep -E 'bl\s'
```

```text
--- NX=false
	bl	operator new(unsigned int)
	bl	T::T(T const&)
	bl	T::~T()
	bl	operator delete(void*, unsigned int)
--- NX=true
	bl	operator new(unsigned int)
	bl	T::T(T&&)
	bl	T::~T()
	bl	operator delete(void*, unsigned int)
```

결론: **맥에서 `-fno-exceptions` 로 돌려서 이동 카운트가 잘 나와도, GCC 툴체인에서는 복사가 불릴 수 있다.** 그래서 예외를 끈 펌웨어에서도 이동 생성자에는 `noexcept` 를 붙인다. 이 차이 때문에 하네스의 `kVectorIgnoresThrowingMove` 는 `_LIBCPP_VERSION` 과 `__cpp_exceptions` 로 기댓값을 나눠 두었다.

**소멸자가 이동을 없애는 함정.** `[class.copy.ctor]` 에 따라 사용자 선언 복사 생성자·복사 대입·이동 대입·소멸자 중 하나라도 있으면 이동 생성자는 암시적으로 선언되지 않는다. 그러면 rvalue 도 `const T&` 복사 생성자에 묶인다.

```cpp
struct DtorBlocksMove { Probe p; explicit DtorBlocksMove(int v) : p(v) {} ~DtorBlocksMove() {} };
DtorBlocksMove b(std::move(a));   // copy_ctor == 1, move_ctor == 0, a.p.value 그대로
```

게다가 `std::is_move_constructible_v<DtorBlocksMove>` 는 **true** 다. 이 trait 은 "rvalue 로 생성할 수 있는가"를 묻기 때문이다. `-Wall -Wextra -Werror` 도 경고를 내지 않았다(Apple clang 21, g++ 15 실측). 카운터나 어셈블리로만 잡힌다.

### Part C: 값 전달 vs 핸들

`route()` 는 hop 하나당 push 1번 + pop 1번으로 이동 2번을 한다. `T` 만 바꿔 측정한 결과다.

| hops | `Frame` 이동 수 | `Frame` 전송 바이트 | 핸들 이동 수 | 핸들 전송 바이트 |
|---:|---:|---:|---:|---:|
| 1 | 2 | 516 | 2 | 2 |
| 2 | 4 | 1032 | 4 | 4 |
| 3 | 6 | **1548** | 6 | **6** |
| 8 | 16 | 4128 | 16 | 16 |

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">3 hop 전달: 프레임이 움직이느냐, 번호표가 움직이느냐</text>
  <text x="10" y="50" fill="currentColor">값 전달</text>
  <rect x="80" y="36" width="130" height="30" class="f-warn" stroke="currentColor"/>
  <text x="96" y="56" fill="currentColor" font-family="monospace" font-size="12">Frame 258B</text>
  <rect x="300" y="36" width="130" height="30" class="f-warn" stroke="currentColor"/>
  <text x="316" y="56" fill="currentColor" font-family="monospace" font-size="12">Frame 258B</text>
  <rect x="520" y="36" width="130" height="30" class="f-warn" stroke="currentColor"/>
  <text x="536" y="56" fill="currentColor" font-family="monospace" font-size="12">Frame 258B</text>
  <path d="M210 51 L300 51 M430 51 L520 51" stroke="currentColor" fill="none"/>
  <text x="80" y="88" fill="currentColor">총 1548바이트 복사, 페이로드 주소가 매번 바뀐다</text>

  <text x="10" y="132" fill="currentColor">핸들 전달</text>
  <rect x="80" y="118" width="130" height="30" class="f-ok" stroke="currentColor"/>
  <text x="96" y="138" fill="currentColor" font-family="monospace" font-size="12">idx=2 (1B)</text>
  <rect x="300" y="118" width="130" height="30" class="f-ok" stroke="currentColor"/>
  <text x="316" y="138" fill="currentColor" font-family="monospace" font-size="12">idx=2 (1B)</text>
  <rect x="520" y="118" width="130" height="30" class="f-ok" stroke="currentColor"/>
  <text x="536" y="138" fill="currentColor" font-family="monospace" font-size="12">idx=2 (1B)</text>
  <path d="M210 133 L300 133 M430 133 L520 133" stroke="currentColor" fill="none"/>
  <rect x="210" y="184" width="320" height="36" class="f-acc" stroke="currentColor"/>
  <text x="226" y="207" fill="currentColor" font-family="monospace" font-size="12">FramePool slots[2]: 258B, 고정 주소</text>
  <path d="M145 148 L300 184 M365 148 L365 184 M585 148 L440 184" stroke="currentColor" fill="none"/>
  <text x="10" y="242" fill="currentColor">총 6바이트. DMA 버퍼 주소가 고정이라 캐시 유지보수나 MPU 영역 설정도 단순해진다.</text>
</svg>
```

Cortex-M4 기계어로 비교한다. `demo_thumb_move.cpp` 는 헤더 없이 쓴 축약판이고, 실제 `clang++ --target=thumbv7em-none-eabi` 출력이다.

```bash
clang++ --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c++20 -O2 -fno-exceptions -fno-rtti -S demo_thumb_move.cpp -o -
```

```text
_Z10copy_frameR5FrameRKS_:
	mov.w	r2, #258
	b	__aeabi_memcpy
_Z10move_frameR5FrameS0_:
	mov.w	r2, #258
	b	__aeabi_memcpy
_Z7move_txR6TxDescS0_:
	push	{r4, r5, r7, lr}
	movs	r2, #64
	mov	r4, r1
	mov	r5, r0
	bl	__aeabi_memcpy
	ldrh.w	r0, [r4, #64]
	strh.w	r0, [r5, #64]
	movs	r0, #0
	strh.w	r0, [r4, #64]
	pop	{r4, r5, r7, pc}
_Z11move_handleR6HandleS0_:
	cmp	r0, r1
	it	eq
	bxeq	lr
	push	{r4, r5, r7, lr}
	mov	r5, r0
	ldrb	r0, [r0]
	mov	r4, r1
	cmp	r0, #255
	it	ne
	blne	_Z9pool_freeh
	ldrb	r0, [r4]
	strb	r0, [r5]
	movs	r0, #255
	strb	r0, [r4]
	pop.w	{r4, r5, r7, lr}
	bx	lr
```

- 복사와 이동은 **글자까지 같다**. 둘 다 258바이트 `memcpy` 로 꼬리 호출한다.
- `ExchangeOnMove` 의 추가 비용은 `movs` + `strh.w`, 명령 2개다.
- 핸들 이동에는 `memcpy` 가 없다. 슬롯 반납 분기 때문에 명령 수가 아주 적지는 않지만, 비용이 페이로드 크기와 무관하다.

**값 매개변수의 숨은 비용.** "이동이니까 괜찮다"며 `consume_frame(std::move(f))` 로 넘기면 어떻게 될까. 인자 객체가 새로 만들어져야 하므로 스택을 쓴다.

```text
_Z8by_valueR5Frame:                  ; clang --target=thumbv7em
	push	{r4, lr}
	sub	sp, #504
	add	r4, sp, #244
	mov	r1, r0
	mov	r0, r4
	mov.w	r2, #258
	bl	__aeabi_memcpy
	...                              ; 16바이트 뒤 나머지 242바이트를 스택 인자 영역으로 복사
	ldm	r3, {r0, r1, r2, r3}
	bl	_Z13consume_frame5Frame
	add	sp, #504
	pop	{r4, pc}
_Z6by_refRK5Frame:                   ; 참조 전달
	b	_Z11consume_refRK5Frame
```

```text
_Z8by_valueR5Frame:                  ; arm-none-eabi-g++ 14.2.1 (같은 소스)
	push	{r4, r5, r6, r7, lr}
	sub	sp, sp, #252
	ldr	r7, [r0]	@ unaligned
	...
	movs	r2, #242
	mov	r0, sp
	bl	memcpy
```

AAPCS 는 앞 16바이트를 r0~r3 로, 나머지를 스택으로 넘긴다. GCC 는 스택 252바이트로 끝냈고, clang 은 임시 객체를 하나 더 만들어 **504바이트**를 썼다. 어느 쪽이든 ISR 스택에 부담스러운 크기다. 처음엔 "258바이트쯤 쓰겠지"라고 예상했는데 clang 결과는 그 두 배였다. 스택 비용은 컴파일러별로 측정해야 한다.

설계 기준을 표로 정리한다.

| 상황 | 추천 | 이유 |
|---|---|---|
| 작고 trivially copyable (레지스터 2개쯤) | 값 전달 | 복사가 곧 이동이고 제일 단순 |
| 소유권 이전 없이 읽기만 | `const T&` / `std::span` (CPP-B11) | 복사 자체가 없다 |
| 큰 버퍼가 단계를 거쳐 이동 | 풀 + 이동 전용 핸들 | 전송 비용이 크기와 무관, 주소 고정 |
| 사본이 정말 필요 | 명시적 복사 | 비용이 코드에 보인다 |
| 이동 후 원본을 재사용할 가능성 | `ExchangeOnMove` 또는 명시적 reset | 이중 처리 방지 |

`Ring::push(T&&)` 가 rvalue 만 받는 것도 설계다. `T` 는 클래스 템플릿 매개변수라서 이것은 전달 참조(forwarding reference)가 아닌 진짜 rvalue 참조이고, `q.push(frame)` 은 컴파일되지 않는다. `demo_errors.cpp` 의 실제 에러다.

```text
$ c++ -std=c++20 -fsyntax-only -DSHOW_ERROR=2 demo_errors.cpp
demo_errors.cpp:34:12: error: rvalue reference to type 'Frame' cannot bind to lvalue of type 'Frame'
   34 |     s.push(f);                    // lvalue 는 T&& 에 묶이지 않는다: 몰래 복사 금지
      |            ^
demo_errors.cpp:20:19: note: passing argument to parameter 'x' here

$ c++ -std=c++20 -fsyntax-only -DSHOW_ERROR=1 demo_errors.cpp
demo_errors.cpp:32:12: error: call to deleted constructor of 'Handle'
   32 |     Handle c(b);                  // 복사 금지: deleted
      |            ^ ~
demo_errors.cpp:11:5: note: 'Handle' has been explicitly marked deleted here
```

하네스에서는 같은 규칙을 `CanPushLvalue` concept 와 `std::is_copy_constructible_v` 의 `static_assert` 로 검사한다.

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution   # 101/101 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer solution.cpp -o solution_asan && ./solution_asan
bash run_matrix.sh                                                                       # 라이브러리 × 예외 표 (Lima VM elinux 필요)
c++ -std=c++20 -O2 -fno-exceptions -fno-rtti demo_bench.cpp -o bench && ./bench          # 시간 (참고용)
```

## 7. 자주 하는 실수

**1. `std::move` 를 붙이면 빨라진다고 믿는다.**
`std::move` 는 캐스트일 뿐이다. 비용은 호출되는 이동 생성자가 결정한다.

```cpp
Frame b = std::move(a);   // thumbv7em: 복사와 똑같이 memcpy 258바이트
Frame& b = a;             // 소유권 이전이 필요 없다면 이게 정답 (복사 0)
```

**2. 디버그 로그용 소멸자를 추가했더니 성능이 떨어진다.**
소멸자 선언으로 암시적 이동이 사라지고 복사로 대체됐다. 컴파일 에러도 경고도 없다.

```cpp
struct Driver {
    std::array<Probe, 16> q;
    ~Driver() { log("bye"); }
    Driver(Driver&&) noexcept = default;            // 소멸자를 쓰면 이 넷을 명시한다
    Driver& operator=(Driver&&) noexcept = default;
    Driver(const Driver&) = default;
    Driver& operator=(const Driver&) = default;
};
```

**3. 맥에서 테스트해 봤으니 `noexcept` 는 없어도 된다고 판단한다.**
libc++ + `-fno-exceptions` 조합에서만 이동이 나온다. arm-none-eabi-g++ 의 libstdc++ 는 복사를 부른다(위 `bl T::T(T const&)`). 이동 생성·대입에는 `noexcept` 를 붙인다.

**4. 이동당한 인라인 객체를 빈 것으로 착각한다.**

```cpp
Frame b(std::move(a));
if (a.len > 0) process(a);   // a.len 은 그대로다. 같은 프레임을 두 번 처리한다
```

`len` 을 `ExchangeOnMove<uint16_t>` 로 두면 `a.len` 이 0 이 된다. 추가 비용은 위 어셈블리의 명령 2개다.

**5. `const` 객체에 `std::move` 를 쓴다.**
`const T&&` 는 이동 생성자에 묶이지 못하고 복사 생성자로 간다(`practice.cpp` 연습 3). 이동 전용 타입이면 `demo_errors.cpp` 의 `SHOW_ERROR=3` 처럼 컴파일 에러로 드러나지만, 복사 가능한 타입이면 조용히 복사된다.

**6. 큰 객체를 값 매개변수로 받고 `std::move` 로 넘긴다.**
위 `by_value` 처럼 clang 은 504바이트, GCC 는 252바이트의 스택을 썼다. `const T&` 또는 핸들로 바꾼다.

**7. 핸들 이동 대입에서 기존 슬롯 반납을 빼먹는다.**

```cpp
FrameHandle& operator=(FrameHandle&& o) noexcept {
    idx_ = std::exchange(o.idx_, kNoFrame);   // 들고 있던 슬롯이 영원히 누수된다
    return *this;
}
```

하네스의 `a = std::move(b)` 뒤 `used_count() == 1` 검사와 자기 대입 검사가 이 실수를 잡는다.

## 8. 면접 Q&A

<details><summary>Q1. std::move 는 무엇을 하나요? 실제로 메모리를 옮기나요?</summary>

`static_cast<T&&>` 와 같은 캐스트이고 혼자서는 아무 명령도 만들지 않는다. 오버로드 해석에서 이동 생성자·대입이 선택되게 할 뿐이다. 실제 비용은 그 연산의 본문이 결정한다. 포인터를 가진 타입이면 포인터 교환이고, 버퍼를 품은 타입이면 그 버퍼 전체를 옮긴다. `const` 객체나 이동 연산이 없는 타입이면 복사가 호출된다.
</details>

<details><summary>Q2. std::array 와 std::vector 의 이동 복잡도는 어떻게 다른가요? 꼬리질문: array 의 이동은 사용 중인 요소만 옮기나요?</summary>

`[container.reqmts]` 기준 이동 생성은 array 가 선형이고 vector 는 상수다. array 는 사용 개수 개념이 없어서 항상 N개 전부를 옮긴다. `InlineOwner<64>` 에 1개만 넣어도 이동이 64번이었다. 사용 개수만큼만 옮기려면 raw storage 위의 `static_vector` 처럼 직접 구현해야 한다 (CPP-M10).
</details>

<details><summary>Q3. noexcept 이동 생성자가 왜 중요한가요? 예외를 끈 펌웨어에서도요?</summary>

표준 컨테이너의 재배치는 강한 예외 보장을 위해 `move_if_noexcept` 규칙을 쓴다. 이동이 `noexcept` 가 아니고 복사가 가능하면 복사한다. `-fno-exceptions` 에서도 `noexcept` 는 타입의 일부이므로 trait 결과는 같다. 실제 vector 동작은 라이브러리마다 달랐다. libc++ 21 은 `#if _LIBCPP_HAS_EXCEPTIONS` 분기로 그냥 이동했고, libstdc++ 는 호스트 15.2 실행과 arm-none-eabi 14.2.1 컴파일 결과 모두 복사를 택했다. 그래서 예외를 꺼도 붙이는 쪽이 안전하고 이식성 있다.
</details>

<details><summary>Q4. move_if_noexcept 가 복사를 고르지 않는 "던질 수 있는 이동"은 어떤 경우인가요?</summary>

복사가 불가능한 이동 전용 타입이다. `T&&` 를 돌려주며 강한 보장은 포기한다(약한 보장). 규칙은 `is_nothrow_move_constructible_v<T> || !is_copy_constructible_v<T>` 이면 `T&&`, 아니면 `const T&` 다. `MoveOnlyThrowing` 이 relocate 에서 8번 이동한 이유다.
</details>

<details><summary>Q5. 소멸자만 추가했는데 왜 이동이 사라지나요? 왜 컴파일러가 알려 주지 않나요?</summary>

사용자 선언 소멸자가 있으면 이동 생성자와 이동 대입은 암시적으로 선언되지 않는다. rvalue 인자는 `const T&` 를 받는 복사 생성자에 묶이므로 코드는 정상적으로 컴파일된다. `is_move_constructible` 도 true 다. 이 레슨의 빌드에서 `-Wall -Wextra -Werror` 도 경고를 내지 않았다. 해결은 이동·복사 네 연산을 `= default` 로 명시하는 것이고, `noexcept` 도 함께 붙인다. 검출은 카운터 테스트나 `is_nothrow_move_constructible` 의 `static_assert` 로 한다.
</details>

<details><summary>Q6. 이동된 객체는 어떤 상태인가요? 꼬리질문: 표준 라이브러리 타입과 내 타입이 다른가요?</summary>

표준 라이브러리 타입은 별도 명시가 없으면 "유효하지만 미지정(valid but unspecified)" 상태다. 사용자 타입은 작성자가 정한 대로다. `std::array<uint8_t,N>` 멤버를 가진 구조체는 이동 뒤 원본이 **그대로 남는다**. `len` 도 그대로라 "두 번 처리" 버그가 난다. `vector` 이동 생성 뒤 비어 있는 것은 실측으로는 맞지만 cppreference 도 명시하지 않는다. 이동한 객체는 다시 대입하거나 소멸시키는 것만 한다고 생각하는 게 안전하다.
</details>

<details><summary>Q7. 256바이트 프레임을 ISR 에서 태스크로 넘겨야 합니다. 어떻게 설계하겠습니까?</summary>

프레임 본체는 고정 주소의 풀에 두고, 큐에는 슬롯 인덱스만 담은 이동 전용 RAII 핸들을 넣는다. 3 hop 기준 1548바이트 전송이 6바이트로 줄고, 페이로드 주소가 고정되어 DMA 버퍼 관리도 쉽다. 핸들이 옮기는 데이터는 1바이트뿐이라 비용이 프레임 크기와 무관하다. 다만 슬롯 반납 분기는 남고, 큐 자체의 ISR 안전성은 별도 문제다(CPP-A04 참고). 풀 고갈은 무효 핸들로 표현하고 호출부에서 처리한다.
</details>

<details><summary>Q8. 값 매개변수 + std::move 패턴을 펌웨어에서 쓰면 안 되는 이유는요?</summary>

매개변수 객체가 새로 생기므로 트리비얼한 구조체의 이동은 곧 복사이고 스택도 쓴다. 258바이트 `Frame` 을 thumbv7em 으로 넘겼을 때 clang 은 `sub sp, #504`, GCC 14 는 `sub sp, sp, #252` 였다. AAPCS 는 앞 16바이트만 레지스터로 보낸다. 포인터 하나만 가진 작은 타입에는 좋은 패턴이지만, 버퍼를 품은 타입에는 `const T&` 나 핸들을 쓴다.
</details>

## 9. 추가 연습

### 연습 1. move_if_noexcept 규칙을 trait 로
`relocates_by_move_v<T>` 를 직접 정의하고 `decltype(std::move_if_noexcept(...))` 결과와 일치하는지 `static_assert` 로 확인한다. `Probe`, `ThrowingProbe`, `MoveOnlyThrowing`, `CopyOnly`, `int`, `std::vector<int>` 를 쓴다.

<details><summary>답</summary>

`std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>` 다. `CopyOnly` 는 이동 생성자가 없어 rvalue 도 복사 생성자로 가며, 그 복사 생성자는 `noexcept` 가 아니므로 false 다.
</details>

### 연습 2. swap 의 비용
`std::swap(a, b)` 로 `InlineOwner` 와 `vector` 소유 객체를 바꿀 때의 이동 횟수를 센다.

<details><summary>답</summary>

`std::swap` 은 임시 생성 1번 + 대입 2번이므로 인라인 쪽은 **3N** 번 이동한다 (N=1/8/32 → 3/24/96). vector 소유 객체는 요소 수와 무관하게 0번이다.
</details>

### 연습 3. const 를 move 하면
`const Probe c(7); Probe d(std::move(c));` 에서 복사 횟수를 확인한다.

<details><summary>답</summary>

1번 복사된다. `std::move(c)` 의 타입은 `const Probe&&` 이고 `Probe&&` 를 받는 이동 생성자에는 묶이지 않는다.
</details>

### 연습 4. reserve 가 없애는 것
`push_back_relocations(64, true/false)` 로 64번 삽입 시 재배치 수를 잰다.

<details><summary>답</summary>

`reserve` 하면 0번이다. 안 하면 libc++ 와 libstdc++ 모두 실측 63번(1+2+4+8+16+32)이었다. 증가 인수는 표준이 정하지 않으므로 테스트는 `>= 32` 로만 검사한다.
</details>

### 연습 5. std::string 의 두 얼굴 (SSO)
짧은 문자열과 200자 문자열을 이동해서 `data()` 주소가 유지되는지 본다. 하나의 타입 안에 인라인과 힙 두 경우가 공존한다.

<details><summary>답</summary>

`data()` 가 객체 안을 가리키면 SSO(짧은 문자열 최적화)다. 이때는 문자를 새 객체로 복사해야 하므로 주소가 바뀐다. 힙이면 포인터를 훔치므로 주소가 유지된다. SSO 존재와 임계값은 구현에 달렸으니 테스트는 "객체 내부인가"를 먼저 판별한다.
</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 15/15 passed
```

## 10. 핵심 정리

- `std::move` 는 캐스트다. 비용은 객체가 **직접 품은 바이트 수**가 결정한다.
- `vector` 이동은 포인터 교환으로 상수 시간, `std::array` 멤버 이동은 **용량 N 만큼** 선형이다.
- trivially copyable 인라인 데이터의 이동과 복사는 thumbv7em 에서 같은 `memcpy` 였다.
- 표준 규칙: `is_nothrow_move_constructible || !is_copy_constructible` 이면 이동, 아니면 복사.
- `-fno-exceptions` 실측: libc++ 는 던질 수 있는 이동도 그냥 이동, libstdc++ 는 `noexcept` 없으면 복사. 그래서 펌웨어에서도 `noexcept` 를 붙인다.
- 소멸자를 선언하면 이동이 조용히 복사가 된다. 네 연산을 `= default` 로 명시한다.
- 이동당한 인라인 객체는 "여전히 유효해 보인다". 길이/상태는 `ExchangeOnMove` 로 비운다.
- 큰 프레임은 **풀 + 이동 전용 핸들**: 3 hop 1548바이트 → 6바이트, 값 매개변수는 스택 252~504바이트를 먹었다.

## 참고자료

- [cppreference — std::move_if_noexcept](https://en.cppreference.com/w/cpp/utility/move_if_noexcept) · [std::vector constructor](https://en.cppreference.com/w/cpp/container/vector/vector) · [Move constructors](https://en.cppreference.com/w/cpp/language/move_constructor) · [std::exchange](https://en.cppreference.com/w/cpp/utility/exchange) · [std::construct_at](https://en.cppreference.com/w/cpp/memory/construct_at)
- C++ draft: [container.reqmts] (이동 생성 복잡도), [class.copy.ctor] (암시적 이동 생성자 조건), [lib.types.movedfrom] (표준 타입의 moved-from 상태)
- [LWG 2321 — Moving containers should (usually) be required to preserve iterators](https://cplusplus.github.io/LWG/issue2321)
- libc++ 소스: `__memory/uninitialized_algorithms.h` 의 `__uninitialized_allocator_relocate` (macOS 26.4 SDK) · libstdc++ 소스: `bits/vector.tcc` 의 `reserve`
- Arm — Procedure Call Standard for the Arm Architecture (AAPCS32): 인자 전달 규칙
- 이 트랙: CPP-B11 (span), CPP-B14 (힙 금지), CPP-M07 (저장소 수명), CPP-M08 (클래스 전용 풀), CPP-M10 (`static_vector`), MCPP-B03 (이동 생성자 작성), MCPP-M10 (예외 비용)
