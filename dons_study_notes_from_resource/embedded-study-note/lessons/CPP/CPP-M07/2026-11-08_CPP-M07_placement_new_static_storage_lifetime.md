---
id: CPP-M07
time: 80분
updated: 2026-11-08
---
# CPP-M07 placement new와 정적 저장소 수명 관리

## 학습 목표

- `alignas(T) std::byte buf[sizeof(T)]` 위에 `::new` 로 객체를 **지연 생성(lazy construction)** 하는 `StaticInstance<T>` 를 구현하고, 빈 상태·생성 중·살아 있음·소멸 중의 상태 전이를 설명한다.
- 생성/소멸 카운터와 주소 장부로 **이중 생성·누락 파괴·이중 파괴**를 잡는 테스트를 만든다. 주소 표만으로는 놓치는 경우가 무엇인지 안다.
- `std::launder` 가 **언제 필요하고, 왜 돌려받은 포인터를 저장하면 필요 없는지** 설명한다. 비용이 0임을 Cortex-M4 어셈블리로 확인한다.
- `std::construct_at` / `destroy_at`, implicit-lifetime 타입, over-aligned 타입에서 달라지는 규칙을 구분한다.
- 펌웨어가 끝나지 않는다는 사실이 소멸자 설계를 어떻게 바꾸는지, ELF 의 `.init_array` 와 `size` 숫자로 말한다.

## 1. 문제

힙이 없는 펌웨어에서 "클럭을 켠 **다음에** 만들어야 하는 전역 드라이버"를 만드는 표준 도구가 placement new 이고, 면접에서는 정렬·수명·`launder` 를 한 번에 묻는 단골 꼬리질문이다.

C 에서는 `static uart_t g_uart;` 를 두고 `uart_init(&g_uart, 115200)` 을 부르면 끝이었다. C++ 객체는 "생성자가 끝나야 수명이 시작"되고([basic.life]), 전역 객체의 생성자는 `main` 전에 돈다(CPP-B09 참고). 클럭·핀이 준비되기 전이다. 그래서 **메모리는 `.bss` 에 미리 잡고, 객체는 `board_init()` 이 고른 시점에 그 위에 만든다.** 대가는 컴파일러가 해 주던 수명 관리를 우리가 떠안는 것이다.

```svg
<svg viewBox="0 0 740 262" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">StaticInstance&lt;T&gt; — 메모리 배치와 상태 전이</text>
  <g stroke="currentColor">
    <rect x="10" y="38" width="300" height="52" class="f-acc"/>
    <rect x="310" y="38" width="120" height="52" class="f-mut"/>
    <rect x="430" y="38" width="130" height="52" class="f-mut"/>
    <rect x="560" y="38" width="170" height="52" class="f-mut"/>
  </g>
  <g fill="currentColor">
    <text x="20" y="59" font-family="monospace" font-size="12">alignas(T) byte storage_[sizeof(T)]</text>
    <text x="20" y="79">객체가 들어갈 자리 (.bss, 0 으로 시작)</text>
    <text x="322" y="59" font-family="monospace" font-size="12">T* ptr_</text>
    <text x="322" y="79">new 의 반환값</text>
    <text x="442" y="59" font-family="monospace" font-size="12">State state_</text>
    <text x="442" y="79">1바이트</text>
    <text x="572" y="59" font-family="monospace" font-size="12">misuse_</text>
    <text x="572" y="79">잘못된 호출 횟수</text>
  </g>
  <g stroke="currentColor">
    <rect x="10" y="130" width="110" height="40" rx="6" class="f-mut"/>
    <rect x="175" y="130" width="130" height="40" rx="6" class="f-warn"/>
    <rect x="360" y="130" width="110" height="40" rx="6" class="f-ok"/>
    <rect x="525" y="130" width="130" height="40" rx="6" class="f-warn"/>
  </g>
  <g fill="currentColor">
    <text x="42" y="155">Empty</text>
    <text x="190" y="155">Constructing</text>
    <text x="395" y="155">Alive</text>
    <text x="545" y="155">Destroying</text>
    <text x="122" y="122" font-size="12">emplace()</text>
    <text x="310" y="122" font-size="12">생성자 끝</text>
    <text x="474" y="122" font-size="12">destroy()</text>
    <text x="240" y="205" font-size="12">소멸자 끝 → Empty</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M120 150 L175 150"/><path d="M305 150 L360 150"/><path d="M470 150 L525 150"/>
    <path d="M590 170 L590 192 L65 192 L65 170"/>
  </g>
  <text x="10" y="230" fill="currentColor">get() 은 Alive 일 때만 non-null 을 돌려준다.</text>
  <text x="10" y="250" fill="currentColor">다른 상태에서 emplace/destroy 를 부르면 거부하고 misuse_ 를 올린다.</text>
</svg>
```

호스트에서 진짜 UART 를 건드릴 수 없으므로 `mock::uart` 구조체를 레지스터처럼 쓰고, "생성·소멸 도중에 IRQ 가 끼어드는" 순간은 드라이버 생성자/소멸자 안에서 `uart_isr()` 를 직접 호출해 결정론적으로 재현한다.

### Part A — `StaticInstance<T>` 의 생성·소멸

- `emplace(args...)`: `Empty` 일 때만 생성하고 포인터를 돌려준다. 이미 있으면 `misuse_++` 하고 `nullptr` (예외를 안 쓰므로 이것이 실패 경로다, CPP-B13 참고). 기존 객체는 건드리지 않는다.
- `destroy()`: `Alive` 일 때만 소멸한다. 아니면 `misuse_++`, `false`.
- `T` 의 생성자에 인자를 그대로 전달한다 (이동 전용 타입, 참조 멤버, 집합체, `int` 포함).
- 복사·이동 금지, 그리고 **소멸자를 선언하지 않는다** (trivially destructible, Part C 참고).

### Part B — 수명 장부와 launder

- `LifeLog::on_ctor/on_dtor` 를 채워 이중 생성(`double_ctor`), 표에 없는 소멸(`bad_dtor`), 살아 있는 개수(`live_n`), 총 생성/소멸 수를 기록한다. 맨 placement new 를 두 번 부르는 코드로 장부가 실제로 버그를 잡는지 확인한다.
- 포인터를 저장하지 않는 `LaunderInstance<T>` 를 쓴다. `reinterpret_cast<T*>(storage_)` 를 왜 그대로 쓸 수 없는지 설명한다.
- `const` 객체 재생성, 64바이트 정렬 타입, 클래스 전용 `operator new` 가 있는 타입, 생성자 안에서 자기 자신을 `emplace` 하는 재진입까지 통과시킨다.

### Part C — ISR 이 보는 UART 드라이버

- `IsrVisible<T>::start/stop/for_isr` — **완전히 생성한 뒤 공개, 공개를 거둔 뒤 소멸.**
- `uart_restart(baud)`: 저전력 복귀 시나리오. 재시작해도 DMA 채널 수는 1 이어야 한다.
- 테스트는 생성·소멸 도중 IRQ 가 드라이버를 보지 못하는 것과, `stop` 없이 다시 생성하면 DMA 채널이 새는 것을 확인한다.

## 2. Problem (English)

> **Deferred construction in static storage, without a heap.**
>
> Implement `template <typename T> class StaticInstance` with storage `alignas(T) std::byte storage_[sizeof(T)]`:
>
> 1. `template <typename... Args> [[nodiscard]] T* emplace(Args&&...)` constructs `T` in place if empty, otherwise returns `nullptr` and leaves the existing object untouched. `bool destroy()` ends the lifetime if alive, otherwise returns `false`. `T* get()` is non-null only while alive, including during re-entrant calls made from `T`'s own constructor or destructor.
> 2. No exceptions, no heap. The class must be non-copyable, trivially destructible even when `T` is not, and usable with `constinit`.
> 3. Write a tracking type that detects double construction at one address, destruction of an untracked address, and missing destructor calls.
> 4. Write a variant that stores no pointer. Explain why `reinterpret_cast<T*>(storage_)` alone is not enough and what `std::launder` adds.
> 5. Support over-aligned `T` (`alignas(64)`) and `T` with a class-specific `operator new`.
> 6. Build `IsrVisible<T>` so an interrupt arriving during construction or destruction sees `nullptr`, and implement a driver restart that does not leak the resources owned by the old instance.
>
> Example: `emplace(11)` → `emplace(22)` returns `nullptr` and the value stays 11; `destroy()` twice → second returns `false`; constructor count 2 and destructor count 1 after a restart.

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 대비
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
# 처음엔 "30/121 passed" — TODO 를 채우면 125/125 가 된다
# (전체 개수가 다른 이유: 일부 CHECK 는 emplace 가 성공했을 때만 실행된다)
```

## 4. 힌트

<details>
<summary>힌트 1 — emplace 의 순서</summary>

상태 확인 → `state_ = Constructing` → `::new (static_cast<void*>(storage_)) T(std::forward<Args>(args)...)` → 반환값을 `ptr_` 에 저장 → `state_ = Alive`. 포인터는 **생성자가 끝난 뒤에** 쓴다.

</details>

<details>
<summary>힌트 2 — destroy 는 거꾸로</summary>

`ptr_` 를 먼저 지역 변수로 옮기고 비운 뒤 `std::destroy_at(p)` 를 부른다. 소멸자 안에서 `get()` 을 불러도 `nullptr` 이 나와야 하기 때문이다.

</details>

<details>
<summary>힌트 3 — 장부: 무엇을 어느 순서로</summary>

`on_ctor`: `ctors` 증가 → 주소가 이미 표에 있으면 `double_ctor` 증가하고 끝 → 아니면 표에 추가. `on_dtor`: `dtors` 증가 → 표에 없으면 `bad_dtor` 증가 → 있으면 마지막 원소를 그 자리에 옮겨 제거.

</details>

<details>
<summary>힌트 4 — 저장소에서 T* 를 다시 만들기</summary>

`reinterpret_cast<T*>(storage_)` 까지는 주소만 바뀐다. 그 포인터는 여전히 `std::byte` 를 가리키는 값이고, `std::launder(...)` 를 거쳐야 그 주소에 사는 `T` 를 가리키는 포인터가 된다.

</details>

<details>
<summary>힌트 5 — ISR 공개 순서</summary>

`start`: `inst_.emplace(...)` 가 끝나고 성공했을 때만 `published_.store(p, relaxed)`. `stop`: `store(nullptr)` 뒤에 `inst_.destroy()`. 두 연산 사이의 컴파일러 재배치를 막는 것이 `std::atomic_signal_fence` 다.

</details>

## 5. 풀이

### Part A — 저장소, 생성, 소멸

**왜 `std::byte` 배열인가.** `unsigned char` 또는 `std::byte` 배열 위에 객체를 만들면 그 배열이 객체에 **저장소를 제공(provides storage)** 하고([intro.object]/3), 배열의 수명은 끝나지 않는다. `std::uint32_t buf[N]` 위에 만들면 저장소를 재사용한 것이 되어 배열 원소의 수명이 끝난다([basic.life]/1).

**왜 `::new` + `void*` 인가.** 세 가지 함정을 한 줄로 막는다.

| 쓴 코드 | 문제 |
|---|---|
| `new (storage_) T(...)` | `T` 에 클래스 전용 `operator new` 가 있으면 클래스 범위 이름 찾기가 전역 placement 를 가려서 컴파일 에러 (아래 실제 에러) |
| `::new (storage_) T(...)` | `storage_` 가 `std::byte*` 로 전달된다. 누군가 전역에 `operator new(size_t, std::byte*)` 를 정의했으면 그쪽이 선택된다 |
| `::new (static_cast<void*>(storage_)) T(...)` | 표준 라이브러리의 `voidify` 와 같은 방식. 항상 `operator new(size_t, void*)` |

`std::construct_at(p, args...)` (C++20)도 내부가 `::new (voidify(*p)) T(std::forward<Args>(args)...)` 이므로 같은 일을 한다 (CPP-B14 표 참고). 차이는 하나: `construct_at` 은 상수 평가에서 쓸 수 있고, placement new 는 C++20 상수 평가에서 쓸 수 없다. 이 레슨은 과제대로 placement new 를 직접 쓰고, `destroy_at` 은 `p->~T()` 대신 쓴다 (`int` 같은 스칼라도 그대로 동작).

`T(args...)` 의 괄호는 `Agg{1,2}` 같은 집합체도 초기화한다 (C++20 P0960). 테스트의 `c.emplace(3, 4)` 가 그 경로다. 단, 괄호 초기화는 좁히기 변환을 막지 않는다.

**실제 에러** (`demo_errors.cpp`, 경로만 잘라냄):

```text
# SHOW_ERROR=3 — :: 없는 new, 클래스 전용 operator new 가 있는 타입
demo_errors.cpp:42:5: error: no matching function for call to 'operator new'
demo_errors.cpp:27:32: note: candidate function not viable: requires 1 argument, but 2 were provided
# SHOW_ERROR=2 — 맞지 않는 인자
demo_errors.cpp:22:39: note: because 'std::constructible_from<Drv, const char (&)[5]>' evaluated to false
```

하네스에서는 같은 내용을 `requires` 로 증명한다: `static_assert(!plain_new_ok<ClassNew> && global_new_ok<ClassNew>)`, `static_assert(!can_emplace<OnlyInt> && can_emplace<OnlyInt, int>)`.

**constinit 에는 `{}` 가 필요하다.** C++20 은 constexpr 생성자가 멤버를 초기화하지 않아도 되도록 완화했지만(P1331), `constinit`/`constexpr` 변수의 **최종 결과**에 초기화되지 않은 하위 객체가 있으면 상수 표현식이 아니다. 실제로 `{}` 를 지우면:

```text
# SHOW_ERROR=4 — Apple clang 21
demo_errors.cpp:29:31: error: variable does not have a constant initializer
demo_errors.cpp:29:31: note: subobject 'storage_' is not initialized
# 같은 코드, arm-none-eabi-g++ 14.2.1
demo_errors.cpp:29:31: error: 'constinit' variable 'g_drv' does not have a constant initializer
demo_errors.cpp:29:31: error: 'StaticInstance<Drv>()' is not a constant expression because it refers to an incompletely initialized variable
```

`{}` 는 0 초기화라서 ELF 에서는 `.bss` 에 들어가고 flash 비용이 없다 (아래 CASE=2 의 `.bss` 항목).

**상태를 왜 4개나 두나.** `ptr_` 만으로는 "생성 중"을 구분할 수 없다. 생성자 안에서 `emplace` 가 다시 불리면 `ptr_` 는 아직 `nullptr` 이므로 같은 저장소에 두 번째 객체를 만들기 시작한다. 실제로 상태 검사 한 줄을 지운 변형을 돌리면 무한 재귀로 **SIGSEGV (exit -11)** 로 죽었다 (재진입 테스트 때문). `ptr_` 를 생성 전에 채운 변형은 이렇게 잡힌다 (아래 M3).

### Part B — 장부가 잡는 것과 launder

**장부는 두 축으로 본다.**

| 신호 | 잡는 버그 | 놓치는 것 |
|---|---|---|
| 주소 표 `double_ctor` | 소멸 없이 같은 주소에 재생성 | 다른 주소의 누락 |
| 주소 표 `bad_dtor` | 이중 파괴, 생성한 적 없는 객체 파괴 | |
| `ctors != dtors` | **누락 파괴 일반** | 총량만 본다 (어느 객체인지는 모름) |
| `live_n` | 끝났는데 살아 있는 객체 | 같은 주소 재생성 후 한 번만 소멸하면 0 |

마지막 줄이 함정이다. 테스트 `Tracked(1)`, `Tracked(2)` 를 같은 주소에 만들고 한 번만 소멸하면 `live_n == 0` 인데 `ctors=2, dtors=1` 이다. 주소 표만 봤다면 "샌 것 없음"으로 통과했을 것이다. Part C 의 DMA 채널 누수가 정확히 이 모양이다.

**storage 재사용과 `launder`.** 표준 문구를 코드 입장에서 정리하면:

```svg
<svg viewBox="0 0 740 256" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">이 포인터로 새 객체에 접근해도 되는가? (C++20 기준)</text>
  <g stroke="currentColor">
    <rect x="10" y="36" width="345" height="36" class="f-mut"/>
    <rect x="10" y="78" width="345" height="36" class="f-mut"/>
    <rect x="10" y="120" width="345" height="36" class="f-mut"/>
    <rect x="10" y="162" width="345" height="36" class="f-mut"/>
    <rect x="10" y="204" width="345" height="36" class="f-mut"/>
    <rect x="365" y="36" width="365" height="36" class="f-ok"/>
    <rect x="365" y="78" width="365" height="36" class="f-warn"/>
    <rect x="365" y="120" width="365" height="36" class="f-ok"/>
    <rect x="365" y="162" width="365" height="36" class="f-warn"/>
    <rect x="365" y="204" width="365" height="36" class="f-warn"/>
  </g>
  <g fill="currentColor">
    <text x="20" y="59" font-family="monospace" font-size="12">T* p = ::new (buf) T(...)</text>
    <text x="20" y="101" font-family="monospace" font-size="12">reinterpret_cast&lt;T*&gt;(buf)</text>
    <text x="20" y="143">같은 타입으로 재생성 후의 옛 T*</text>
    <text x="20" y="185">complete const 객체를 재생성한 뒤의 옛 포인터</text>
    <text x="20" y="227">베이스 서브객체 재생성 / 다른 타입으로 재생성</text>
    <text x="375" y="59">그대로 사용 (launder 불필요)</text>
    <text x="375" y="101">아직 byte 를 가리킴 → launder 필요</text>
    <text x="375" y="143">자동으로 새 객체를 가리킴 (C++20 규칙)</text>
    <text x="375" y="185">자동 전환 안 됨 → 새 포인터 또는 launder</text>
    <text x="375" y="227">자동 전환 안 됨 → 새 포인터 또는 launder</text>
  </g>
</svg>
```

- 2번째 줄이 핵심이다. `reinterpret_cast` 는 **주소가 같은 다른 타입의 포인터**를 만들 뿐, 값은 여전히 byte 원소를 가리킨다. `std::byte` 배열과 그 위의 `T` 는 pointer-interconvertible 이 아니다([basic.compound]). `std::launder` 는 "이 주소에 사는 `T` 를 가리키는 포인터"를 돌려준다([ptr.launder]). 전제는 그 주소에 실제로 수명 안의 `T` 가 있는 것이다.
- `new` 의 반환값은 처음부터 새 객체를 가리키므로 **저장해 두면 launder 를 쓸 일이 없다.** `StaticInstance` 가 택한 길이다.
- 3~5번째 줄의 근거는 [basic.life] 의 transparently replaceable 규칙이다. C++17 에서는 `const`/참조 **멤버**가 있어도 launder 가 필요했는데, C++20 에서 완화되어 지금은 complete const 객체와 베이스 클래스 서브객체(potentially-overlapping) 재생성만 남았다.

**비용과 관찰.** `launder` 는 런타임 연산이 아니다. Cortex-M4 로 컴파일한 실제 결과 (`asm_m4.sh`, 지시어 제거):

```text
read_cast:                        read_launder:
    ldr  r0, [r0]                     ldr  r0, [r0]
    bx   lr                           bx   lr
get_ptr:                          get_launder:
    ldr  r0, [r0, #8]                 ldrb r1, [r0, #8]
    bx   lr                           cmp  r1, #0
                                      it   eq
                                      moveq r0, r1
                                      bx   lr
```

`launder` 호출 자체가 만든 명령은 **0개**다. 저장 방식 비교는 이렇다.

| | `StaticInstance` (포인터 저장) | `LaunderInstance` (bool) |
|---|---|---|
| RAM, 4바이트 `T`, Cortex-M4 (실측) | 12바이트 | 8바이트 |
| RAM, 4바이트 `T`, 호스트 arm64 (실측) | 24바이트 | 8바이트 |
| `get()` | `ldr` 1개 | `ldrb` + 조건 분기 없는 선택 |
| 재진입 보호 | 있음 (4-state) | 없음 (`bool` 이라 생성 중을 구분 못 함) |
| 코드 실수 위험 | 낮음 | `launder` 를 빼먹기 쉽다 |

`alignas(64)` 타입에서는 어느 쪽이든 128바이트로 같다 (실측). 상태 몇 바이트에 패딩이 붙기 때문이다. DMA 디스크립터처럼 정렬이 큰 타입을 여러 개 둘 때는 상태를 저장소 밖으로 빼서 패딩을 없앤다 (연습 3).

**launder 를 안 쓰면 실제로 깨지나.** `demo_launder.cpp` 는 `A` 위에 `B` 를 만든 뒤 옛 포인터로 가상 호출을 한다. `-O2` 의 실제 출력:

```text
# 기본 플래그
before=1 stale=2 fresh=2 launder=2 same_addr=1
# -fstrict-vtable-pointers 추가
before=1 stale=1 fresh=2 launder=2 same_addr=1
```

기본 설정에서는 낡은 포인터가 "우연히" 맞게 동작했다. vptr 불변식을 최적화에 쓰는 플래그를 켜자 같은 소스의 `stale` 이 **1** 로 바뀌었다: 컴파일러가 `stale->id()` 를 첫 번째 호출의 결과로 재사용한 것이다. 이것이 UB 의 전형적인 모습이다. 지금 돌아가도 플래그나 컴파일러를 바꾸면 깨진다. `launder` 와 새 포인터는 두 설정 모두 정답을 준다.

**implicit-lifetime 타입.** `int`, POD 구조체 같은 타입은 placement new 없이도 객체가 "암시적으로 생길" 수 있다 ([intro.object]). `std::byte` 배열의 수명 시작, `malloc`, `memcpy` 의 목적지가 그 연산이다 (`memcpy` 는 [cstring.syn]). 조건은 [class.prop]: 사용자 제공 소멸자가 없는 집합체, 또는 trivial eligible 생성자 하나 이상 + trivial 소멸자.

| 타입 (`demo_implicit_lifetime.cpp`, C++23 trait, 실측) | implicit-lifetime | trivially copyable |
|---|---|---|
| `int`, `struct Pod { uint32_t a; }` | 1 | 1 |
| 집합체 + 멤버에 사용자 생성자 (`AggUserCtorMem`) | 1 | 1 |
| 집합체 + 사용자 소멸자 (`AggUserDtor`) | 0 | 0 |
| 사용자 생성자만 (`UserCtorOnly`, 복사 생성자는 trivial) | 1 | 1 |
| 생성자·소멸자 있는 `Driver`, 가상 함수 타입 | 0 | 0 |
| `Pod[4]` | 1 | 1 |
| `Driver[4]` | **1** | 0 |

주의할 점: 마지막 줄처럼 배열 타입은 원소와 무관하게 implicit-lifetime 이다 (배열 객체만 생기고 `Driver` 원소의 수명은 시작되지 않는다). 그래서 드라이버처럼 생성자에 의미가 있는 타입은 implicit-lifetime 이 아니고 **placement new 가 필수**다. 반대로 패킷 헤더 같은 POD 는 `memcpy` 로 채운 뒤 `std::launder(reinterpret_cast<const Hdr*>(buf))` 로 볼 수 있다 (연습 4).

C++23 `std::start_lifetime_as<T>` 가 이 용도의 전용 도구인데 **이 환경에서는 쓸 수 없다**. 직접 확인한 결과 `__cpp_lib_start_lifetime_as` 는 Apple clang 21/libc++ 와 libstdc++ 14.2.1 (-std=c++23) 어디에도 정의되어 있지 않았다. `std::is_implicit_lifetime` 은 libc++ 쪽만 있었고 libstdc++ 14.2.1 에는 없었다.

실제 flash 주소(`0x0800'0000`)의 메타데이터를 읽는 경우는 이 규칙 바깥이다: C++ 객체가 아니므로 레지스터처럼 구현 정의 동작에 기대게 된다 (CPP-B12 참고).

**over-aligned 타입.** `alignas(64)` 처럼 `alignof(std::max_align_t)` 보다 큰 정렬은 extended alignment 라서, 지원 범위가 구현 정의다([basic.align]). 전역 저장소에서 실제로 지켜졌는지 ELF 로 확인했다 (CASE=4):

```text
.bss              NOBITS          00018040 008040 000080 00  WA  0   0 64
00018040 B g_desc
```

`.bss` 정렬 64, 주소 `0x18040` 은 64의 배수, 크기 0x80 = 128바이트. placement new 쪽 규칙도 짚어 둔다: `T` 가 new-extended alignment 이면 컴파일러는 먼저 `align_val_t` 인자를 넣어 찾고, 없으면 그 인자를 뺀 `operator new(size_t, void*)` 로 되돌아간다([expr.new]). 그래서 `Over64` 도 그냥 컴파일되고 할당도 일어나지 않는다. **`alignas(T)` 를 빼면** `Over64` 의 정렬 static_assert 가 먼저 막는다:

```text
M5_no_alignas.cpp:264:15: error: static assertion failed due to requirement 'alignof(StaticInstance<Over64>) == 64'
M5_no_alignas.cpp:264:47: note: expression evaluates to '8 == 64'
```

그냥 `char` 뒤에 놓인 비정렬 저장소에 객체를 만들면 맥 UBSan 은 바로 알려 준다 (`demo_sanitizers.cpp` CASE=1):

```text
demo_sanitizers.cpp:20:53: runtime error: constructor call on misaligned address 0x000100824081 for type 'Regs', which requires 8 byte alignment
demo_sanitizers.cpp:21:122: runtime error: load of misaligned address 0x000100824081 for type 'std::uint64_t' (aka 'unsigned long long'), which requires 8 byte alignment
addr % 8 = 1, a=1
```

맥 호스트에서는 UBSan 이 없으면 조용히 동작한다. Cortex-M0 이면 비정렬 접근은 HardFault 이고 M4 는 LDRD/LDM 에서 fault 난다 (CPP-B12 참고).

**도구가 잡지 못하는 것.** `destroy` 뒤의 접근은 ASan 이 **기본으로는 못 잡는다**. 저장소는 전역 배열이라 ASan 입장에서 멀쩡한 메모리다.

```text
# CASE=2: -fsanitize=address, 소멸 후 r->a 읽기
after destroy: a=1
# CASE=3: destroy 뒤에 ASAN_POISON_MEMORY_REGION(g_buf, sizeof g_buf)
==43320==ERROR: AddressSanitizer: use-after-poison on address 0x000104f2c0a0 ...
READ of size 8 at 0x000104f2c0a0 thread T0
    #0 0x000104f248e0 in main demo_sanitizers.cpp:31
0x000104f2c0a0 is located 0 bytes inside of global variable 'g_buf' defined in 'demo_sanitizers.cpp' (0x000104f2c0a0) of size 16
```

호스트 테스트 빌드에서 `destroy` 에 poison 호출을 넣어 두면 소멸 후 접근이 잡힌다. 상수 평가는 더 엄격해서 이중 소멸과 해제 누락을 컴파일 에러로 알려 준다 (`SHOW_ERROR=5`, `6`):

```text
construct_at.h:61:11: note: destruction of object outside its lifetime is not allowed in a constant expression
demo_errors.cpp:60:14: note: allocation performed here was not deallocated
```

**소멸자 없이 재사용하면.** 저장소를 그냥 재사용하면 이전 객체의 수명은 소멸자 없이 끝난다. C++20 문구는 "그 소멸자의 부수 효과에 의존하면 UB"라고 적었고, 현재 표준 초안의 [basic.life] 에는 그 UB 문장이 없고 소멸자가 불려야 올바른 프로그램이 많다는 note 만 있다 (어떤 이슈가 문구를 바꾸었는지는 미확인). 어느 쪽이든 결론은 같다: DMA 채널을 돌려주는 소멸자를 건너뛰면 Part C 의 누수가 생긴다.

### Part C — 공개 순서, 재시작, 프로그램 종료

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">start / stop 과 ISR 이 보는 값</text>
  <text x="10" y="62" fill="currentColor">main</text>
  <g stroke="currentColor">
    <rect x="70" y="40" width="170" height="36" class="f-warn"/>
    <rect x="240" y="40" width="70" height="36" class="f-acc"/>
    <rect x="310" y="40" width="130" height="36" class="f-ok"/>
    <rect x="440" y="40" width="70" height="36" class="f-acc"/>
    <rect x="510" y="40" width="170" height="36" class="f-warn"/>
  </g>
  <g fill="currentColor">
    <text x="104" y="63">생성자 실행</text>
    <text x="256" y="63">공개</text>
    <text x="350" y="63">사용 중</text>
    <text x="453" y="63">회수</text>
    <text x="548" y="63">소멸자 실행</text>
  </g>
  <text x="10" y="122" fill="currentColor">ISR</text>
  <g stroke="currentColor">
    <rect x="70" y="100" width="240" height="36" class="f-mut"/>
    <rect x="310" y="100" width="130" height="36" class="f-ok"/>
    <rect x="440" y="100" width="240" height="36" class="f-mut"/>
  </g>
  <g fill="currentColor">
    <text x="132" y="123">for_isr() == nullptr</text>
    <text x="325" y="123">드라이버를 본다</text>
    <text x="502" y="123">for_isr() == nullptr</text>
  </g>
  <text x="10" y="170" fill="currentColor">잘못된 순서 ①: 공개 → 생성. ISR 이 절반만 만들어진 객체를 건드린다 (테스트 M2 실패).</text>
  <text x="10" y="194" fill="currentColor">잘못된 순서 ②: 소멸 → 회수. ISR 이 해체 중인 객체를 건드린다 (테스트 M1 실패).</text>
  <text x="10" y="218" fill="currentColor">단일 코어 전제: main 이 멈춘 사이에 ISR 이 끝까지 실행되므로,</text>
  <text x="10" y="238" fill="currentColor">회수 이후에는 ISR 이 그 객체를 쥐고 있을 수 없다.</text>
  <text x="10" y="266" fill="currentColor">멀티코어에서는 회수만으로 부족하다: 다른 코어가 이미 포인터를 읽고 작업 중일 수 있다</text>
  <text x="10" y="286" fill="currentColor">(대기 또는 RCU 류의 유예 기간이 필요, OS-A03 참고).</text>
</svg>
```

**왜 `std::atomic<T*>` 인데 `relaxed` 인가.** `start` 는 thread mode 에서, `for_isr` 는 같은 코어의 인터럽트에서 실행된다. 같은 CPU 의 문맥 전환이므로 필요한 것은 **컴파일러가 순서를 바꾸지 못하게 하는 것**이고, CPU 배리어는 필요 없다. 그 도구가 `std::atomic_signal_fence`(`[atomics.fences]`)다. Cortex-M4 실제 비교:

```text
isr_signal_fence:                 isr_acquire:
    ldr  r0, [r0]                     ldr  r0, [r0]
    bx   lr                           dmb  sy
                                      bx   lr
```

ISR 경로마다 `dmb` 한 개를 아낀다. 다중 코어나 DMA 마스터와 공유한다면 `acquire`/`release` 와 실제 배리어로 바꿔야 한다 (`std::atomic` 의 Cortex-M 동작 전반은 CPP-A04 참고). 주의: 이 하네스는 **순서가 맞는지**만 결정론적으로 확인한다. 컴파일러 재배치가 실제로 막혔는지는 위 어셈블리 읽기로 확인한 것이지 테스트로 증명한 것은 아니다.

`stop` 은 `store` 뒤에 `seq_cst` 펜스를 쓴다. 소멸자 본문이 `store` 앞으로 올라가면 안 되는데, release 저장은 그 방향을 막아 주지 않기 때문이다.

**순서 버그를 일부러 넣어 본 결과** (실제 실행, 변형은 임시 파일이라 저장소에 없다):

```text
===== M1: stop 에서 소멸을 먼저 (회수는 나중)
FAIL M1_stop_destroy_first.cpp:433  mock::isr_saw_null == 3
124/125 passed
===== M2: start 에서 공개를 먼저
FAIL M2_start_publish_first.cpp:417  mock::isr_saw_null == 1 && mock::isr_handled == 0
FAIL M2_start_publish_first.cpp:420  d && mock::isr_handled == 1 && d->rx_count() == 1
FAIL M2_start_publish_first.cpp:428  d && d->rx_count() == 0
FAIL M2_start_publish_first.cpp:429  mock::isr_saw_null == 3
121/125 passed
===== M3: ptr_ 와 state_ 를 생성자 전에 채움
FAIL M3_ptr_before_ctor.cpp:407  g_self_inner_rejected == 1 && g_self_saw_empty == 1
FAIL M3_ptr_before_ctor.cpp:408  g_self.destroy() && g_self_dtor_rejected == 1 && g_self_saw_empty == 2
123/125 passed
```

**재시작 시 `stop` 을 빼먹으면.** 하네스 마지막 블록이 맨 placement new 로 재현한다: `dma_claims == 2`, `double_ctor == 1`. 한 번 소멸해도 `dma_claims == 1` 로 남는다. 채널 수만큼 재시작하면 DMA 채널이 고갈되는 전형적인 "오래 돌리면 죽는" 버그다.

**프로그램이 끝나지 않는다는 것.** 호스트에서 전역 소멸 순서를 찍어 보면 (`demo_order.cpp`, 실측):

```text
  ctor global first
  ctor global second
main start
  ctor function-local (first call)
  ctor StaticInstance-style (never destroyed)
main end
  dtor function-local (first call)
  atexit hook
  dtor global second
  dtor global first
```

생성의 역순이고([basic.start.term]) `atexit` 등록 시점과 함수 내부 static 의 생성 시점이 섞여 순서가 결정된다. `StaticInstance` 형태만 `dtor` 줄이 없다. 소멸자가 없으니 호출될 것이 없기 때문이다.

펌웨어는 `main` 을 돌아오지 않고, 끝나는 순간은 전원 차단이나 reset 이다. **전역 소멸자는 영원히 불리지 않을 코드**인데도 비용은 든다. arm-none-eabi-g++ 14.2.1, `-O2 --gc-sections`, Cortex-M4 로 링크해서 비교한 실측 (`build_teardown.sh`):

| CASE | text | data | bss | `.init_array` | `__dso_handle`, `_fini` 정의 필요 |
|---|---:|---:|---:|---|---|
| 1. 평범한 전역 `Driver g_drv(115200)` | 272 | 8 | 420 | 8바이트 | 필요 |
| 2. `constinit StaticInstance<Driver>`, 소멸자 없음 | **40** | **0** | **16** | 없음 | 불필요 |
| 3. CASE 2 + `~StaticInstance()` 자동 파괴 | 288 | 8 | 424 | 8바이트 | 필요 |
| 4. CASE 2, `alignas(64)` 디스크립터 | 32 | 0 | 128 | 없음 | 불필요 |

CASE 1·3 의 숫자는 `__aeabi_atexit` → `__cxa_atexit` → `__register_exitproc` 와 400바이트 `__atexit0` 표(newlib) 때문이다. `.init_array` 8바이트 = 엔트리 2개(`_GLOBAL__sub_I_g_drv`, newlib 의 `register_fini` — `objdump -s` 로 주소 확인). 링크도 그냥 되지 않았다:

```text
=== CASE=3
link without stubs: FAILED
  undefined: `__dso_handle'
  undefined: `_fini'
```

(이 데모는 `-nostartfiles` 라서 `_fini` 가 없고, `.init_array` 는 실행되지 않는다. 일반 CRT 를 쓰면 링크는 되지만 크기 비용은 그대로다.) `constinit`, 소멸자 없는 CASE 2 의 `_start` 실제 디스어셈블리는 "이미 있나" 검사와 쓰기 4개(포인터, `baud` 멤버, BRR, CR)가 전부다:

```text
00008000 <_start>:
    8000:  ldr    r3, [pc, #24]
    8002:  ldr    r2, [r3, #4]          ; ptr_ 확인
    8004:  cbnz   r2, 8018
    8006:  ldr    r4, [pc, #24]
    8008:  ldr    r1, [pc, #24]
    800a:  str    r3, [r3, #4]          ; ptr_ = storage (주소가 곧 객체)
    800c:  mov.w  r2, #115200
    8010:  movs   r0, #1
    8012:  str    r2, [r4, #0]          ; UART_BRR
    8014:  str    r2, [r3, #0]          ; baud 멤버
    8016:  str    r0, [r1, #0]          ; UART_CR
```

(objdump 의 `@` 주석과 뒤의 무한 루프·literal pool 은 생략, `;` 주석은 설명용으로 붙임.) C 스타일 init 과 비교하면 placement new 자체는 **같은 명령**이다(`demo_codegen.cpp`):

```text
c_init:                 place_only:
    str r1, [r0]            str r1, [r0]
    movs r1, #0             movs r1, #0
    str r1, [r0, #4]        str r1, [r0, #4]
    bx lr                   bx lr
```

`emplace_ptr` (실측 12개 명령)에는 여기에 `ldr/cmp/beq` 검사와 포인터 저장 `str` 이 더 붙는다. 정리하면 **소멸자 없는 StaticInstance + 명시적 `destroy()`** 가 펌웨어의 기본형이고, 재초기화·저전력 복귀처럼 소멸이 진짜 필요한 곳에서만 `destroy()` 를 부른다. 다 쓴 뒤 소멸자가 불리지 않는 것은 설계다. 다만 reset 이 없는 부트로더→앱 점프, 저전력 복귀 경로에서는 끄지 않은 주변장치가 그대로 남으므로 `destroy()` 를 호출할 지점을 정해 두어야 한다. 함수 내부 `static` 은 가드 코드가 붙고 첫 호출 시점이 불명확해서 드라이버에는 맞지 않는다 (CPP-B09 참고).

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution   # 125/125 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer solution.cpp -o sol_san && ./sol_san   # 125/125, 보고 없음

# 관찰용 데모 (전부 실행해서 위에 붙인 출력을 얻었다)
for n in 1 2 3 4 5 6; do c++ -std=c++20 -fno-exceptions -fno-rtti -fsyntax-only -DSHOW_ERROR=$n demo_errors.cpp; done
c++ -std=c++20 -O2 -fno-rtti -fno-exceptions -fstrict-vtable-pointers demo_launder.cpp -o /tmp/ld && /tmp/ld
c++ -std=c++20 -g -O0 -fno-exceptions -fno-rtti -fsanitize=address -DCASE=3 demo_sanitizers.cpp -o /tmp/s3 && /tmp/s3
c++ -std=c++23 -fno-exceptions -fno-rtti demo_implicit_lifetime.cpp -o /tmp/il && /tmp/il
c++ -std=c++20 -O1 -fno-exceptions -fno-rtti demo_order.cpp -o /tmp/ord && /tmp/ord
ARM_INC=/tmp/arm-none-eabi-include bash asm_m4.sh     # 없으면 Lima VM elinux 에서 헤더를 가져온다
limactl shell elinux -- bash -c 'rm -rf /tmp/CPP-M07; mkdir -p /tmp/CPP-M07 && cp ./* /tmp/CPP-M07/ && bash /tmp/CPP-M07/build_teardown.sh'
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `std::byte buf[sizeof(T)];` (alignas 없음) | `char` 뒤에 놓이면 비정렬. M0 는 HardFault, M4 는 LDRD/LDM fault, 호스트는 조용히 동작 | `alignas(T) std::byte buf[sizeof(T)]{}` |
| `new (buf) T(...)` | 클래스 전용 `operator new` 가 전역 placement 를 가려 컴파일 에러 | `::new (static_cast<void*>(buf)) T(...)` |
| `reinterpret_cast<T*>(buf)->x` | 포인터가 여전히 byte 를 가리킴. 옵티마이저 가정이 바뀌면 깨짐 (`-fstrict-vtable-pointers` 실측) | 돌려받은 포인터 저장 또는 `std::launder` |
| 재초기화를 그냥 `new` 로 | 이전 소멸자 누락 → 자원 누수. 컴파일러는 경고 없음 | `destroy()` 후 `emplace()`, 또는 `reemplace` |
| `get()` 이 `state_` 와 무관하게 `ptr_` 반환 후 소멸 뒤 비우기 | 소멸자 안에서 `get()` 이 소멸 중 객체를 돌려줌 | 소멸 전에 `ptr_ = nullptr` |
| 공개를 생성 전에 / 회수를 소멸 후에 | ISR 이 절반 상태의 객체를 사용 (M1, M2) | 생성 → 공개, 회수 → 소멸 |
| 전역에 `~StaticInstance()` 를 달아 편하게 | `__aeabi_atexit`, `__atexit0` 400바이트, `.init_array` — 안 쓰일 코드 (CASE 3) | 소멸자를 두지 않고 명시적 `destroy()` |
| `constinit` 이 안 된다 → `{}` 를 빼 먹음 | 미초기화 하위 객체가 있으면 상수 표현식이 아니다 | 멤버에 `{}` |
| `s.replace(s.get()->value)` 를 참조 그대로 전달 | 소멸 뒤 인자가 댕글링 (`std::optional::emplace` 와 같은 함정) | 인자를 먼저 값으로 복사 (연습 2) |
| 누수를 주소 표로만 판단 | 같은 주소 재생성 후 1회 소멸은 `live_n==0` | `ctors == dtors` 도 함께 검사 |

## 8. 면접 Q&A

<details>
<summary>Q1. placement new 와 일반 new 의 차이는? 이 코드의 런타임 비용은?</summary>

일반 `new T` 는 `operator new` 로 메모리를 얻고 생성자를 부른다. placement new `new (p) T` 는 메모리 획득 없이 `p` 에 생성자만 부른다. `operator new(size_t, void*)` 는 `p` 를 그대로 돌려주는 `noexcept` 함수이고 교체 불가([new.delete.placement]). 이번 Cortex-M4 실측에서 `place_only` 와 C 스타일 `c_init` 은 **같은 명령 4개**였다. 꼬리질문: "그럼 `delete p` 를 해도 되나?" 안 된다. 힙 메모리가 아니므로 `p->~T()` 또는 `std::destroy_at(p)` 만 호출한다.

</details>

<details>
<summary>Q2. std::launder 는 언제 필요한가요? 안 쓰면 실제로 문제가 되나요?</summary>

"주소는 맞지만 그 포인터가 새 객체를 가리키지 않는" 경우에 필요하다. 대표적으로 저장소 포인터를 `reinterpret_cast<T*>` 한 경우, complete const 객체나 베이스 서브객체를 재생성한 뒤의 옛 포인터. `new` 의 반환값을 저장하면 필요 없다. 실제로 깨지는지는 최적화 플래그에 달렸다: clang 기본 설정에서는 동작했지만 `-fstrict-vtable-pointers` 를 켜자 옛 포인터의 가상 호출이 낡은 결과를 돌려주었다 (`stale=1`). 비용은 M4 에서 명령 0개.

</details>

<details>
<summary>Q3. 왜 std::optional 대신 직접 만드나요?</summary>

`constinit std::optional<T>` 도 많은 경우 잘 동작한다 (CPP-B09 연습). 직접 만드는 이유는 (1) 생성/소멸 중 상태 구분과 재진입 거부, (2) ISR 공개 순서, (3) 잘못된 호출 집계 같은 **수명 정책**을 넣기 위해서다. `optional::emplace` 는 있으면 먼저 파괴하고 새로 만든다. 드라이버에서 이것은 조용한 재초기화 경로가 된다. `value()` 가 예외를 던지는 점도 `-fno-exceptions` 에서 주의할 부분이다. 요구가 단순하면 `optional` 이 맞는 답이다.

</details>

<details>
<summary>Q4. 전역 드라이버에 소멸자를 달아야 하나요? 언제 호출되나요?</summary>

호스트에서는 `main` 복귀나 `exit()` 시 생성 역순으로 호출된다([basic.start.term]). 펌웨어는 reset 으로 끝나므로 **호출되지 않는 코드**인데, 달아 두면 실측에서 `.text` 272~288바이트(CASE 2 는 40), `.bss` 약 400바이트가 추가되고 `__dso_handle`, `_fini` 없이는 링크도 안 됐다. 그래서 소멸자를 두지 않고 명시적 `destroy()` 를 쓴다. 소멸이 의미 있는 경로(재초기화, 저전력 복귀, 부트로더→앱 점프)에서 직접 부른다.

</details>

<details>
<summary>Q5. alignas(64) 타입을 StaticInstance 에 넣으면 sizeof 는 얼마인가요? 왜요?</summary>

128바이트다 (host/M4 실측). 멤버 `ptr_` 등이 저장소 뒤에 붙고, 클래스 정렬이 64이므로 크기가 64의 배수로 올라간다. 상태 몇 바이트 때문에 50바이트 이상이 패딩으로 사라진다. 슬롯 N개면 표식을 저장소 밖 별도 비트맵으로 빼서 패딩을 없앤다. 꼬리질문 "placement new 가 over-aligned 를 처리하나?" — 컴파일러가 먼저 `align_val_t` 인자를 넣은 후보를 찾고, 없으면 그 인자를 뺀 `operator new(size_t, void*)` 를 쓴다([expr.new]). 정렬은 저장소가 책임진다.

</details>

<details>
<summary>Q6. uint8_t 배열을 받아서 구조체로 읽을 때 placement new 를 써야 하나요?</summary>

타입에 따라 다르다. implicit-lifetime 타입(POD 헤더)은 `memcpy` 로 채우면 목적지에 객체가 암시적으로 생기므로([intro.object], [cstring.syn]) `std::launder(reinterpret_cast<const Hdr*>(buf))` 로 볼 수 있다. 가장 안전한 방법은 `Hdr h; memcpy(&h, buf, sizeof h);` 복사다: 정렬 조건도 없고 -O2 에서 load 몇 개로 줄어든다. 생성자에 의미가 있는 타입은 implicit-lifetime 이 아니고 placement new 가 필수다. C++23 `std::start_lifetime_as` 가 전용 도구지만 이번 툴체인 (Apple libc++, libstdc++ 14.2.1) 에는 아직 없었다.

</details>

<details>
<summary>Q7. 같은 주소에 다시 생성하는 코드는 UB 인가요?</summary>

저장소 재사용 자체는 합법이다: 이전 객체의 수명은 소멸자 없이 끝난다([basic.life]). 문제는 소멸자의 부수 효과에 기대는 경우다. C++20 문구는 이를 UB 라고 했고 현재 draft 에는 UB 문장이 없지만, 자원 누수는 어느 쪽이든 그대로다. 그리고 옛 포인터의 사용은 위 launder 규칙을 따른다. 그래서 테스트는 "UB 인가"가 아니라 "생성·소멸 수가 맞는가"를 본다.

</details>

## 9. 추가 연습

### 연습 1 — ScopedInstance: 스코프 RAII 가드

`StaticInstance<T>` 를 받아 생성자에서 `emplace`, 소멸자에서 `destroy` 하는 가드를 만들어라. 이미 차 있으면 가드는 실패로 표시되고, 소멸 때 남의 객체를 소멸하면 안 된다. 조기 `return` 경로도 테스트한다.

<details>
<summary>답 보기</summary>

`emplace` 의 반환값을 멤버 `p_` 에 저장하고, `~ScopedInstance` 에서 `if (p_) s_.destroy()` 만 호출한다. 이 조건이 없으면 실패한 가드가 다른 가드의 객체를 파괴한다. 복사는 금지. RAII 자체는 CPP-B06/B07 참고, 정답은 `practice.cpp`.

</details>

### 연습 2 — 자기 내부를 인자로 넘겨 다시 생성하기

`replace(args...)` 를 만들어 `s.replace(s.get()->value)` 처럼 **살아 있는 객체 내부를 가리키는 인자**도 안전하게 처리하라.

<details>
<summary>답 보기</summary>

`destroy()` 뒤의 `args` 는 이미 사라진 객체를 가리킨다. 인자를 먼저 `std::tuple<std::decay_t<A>...>` 로 값 복사한 뒤 파괴하고, `std::apply` 로 `emplace` 에 넘긴다. 복사가 비싸면 인터페이스를 값 인자로 바꾼다. 잘못된 판은 실행하면 UB 라서 테스트에 넣지 않았다.

</details>

### 연습 3 — `Slots<T, N>`: 비트맵 슬롯

`Slots<T, N>` 의 `acquire`/`release` 를 구현하라. 가득 찼을 때 생성자가 불리면 안 되고, 이중 해제와 슬롯 밖 포인터를 거부해야 한다. `alignas(64)` 타입에서 `sizeof` 를 재라.

<details>
<summary>답 보기</summary>

저장소는 `alignas(T) std::byte storage_[N * sizeof(T)]`, 사용 표시는 `uint32_t` 비트맵. 범위 검사는 `uintptr_t` 로 비교한다 (다른 배열 포인터끼리 관계 비교·뺄셈은 UB 가 될 수 있다). `Slots<Desc64, 4>` 의 `sizeof` 는 `5 * 64 = 320`: 비트맵 4바이트에 패딩이 붙어 한 칸 분량이 사라진다. 패딩을 없애려면 비트맵을 별도 객체로 둔다. 빈 칸 찾기는 O(N) 이고, 풀 할당의 free-list 구현은 DS 트랙, 클래스 전용 `operator new` 는 CPP-M08 이 맡는다.

</details>

### 연습 4 — 받은 패킷을 헤더로 보기

`load_header` (복사 경로)와 `view_header` (제자리 경로)를 구현하라. 제자리 경로는 정렬이 안 맞으면 거부해야 하고, 복사 경로는 비정렬도 받아야 한다.

<details>
<summary>답 보기</summary>

`view_header`: 정렬·용량 검사 → `memcpy(rx, image, n)` → `std::launder(reinterpret_cast<const FwHeader*>(rx))` → magic 확인. `memcpy` 의 암시적 객체 생성 덕분에 합법이고, `load_header` 는 `FwHeader` 지역 변수로 복사한다. 비정렬 수신 버퍼가 흔한 환경이면 복사 경로가 기본이다.

</details>

### 연습 5 — 컴파일러에게 수명 검사 맡기기

`std::construct_at`/`destroy_at` 으로 `constexpr` 함수에서 생성·소멸·재사용을 수행하고 `static_assert` 하라. 이중 소멸이 상수 표현식이 아니게 되는 것을 `requires` 로 증명하라.

<details>
<summary>답 보기</summary>

`std::allocator<T>::allocate` + `construct_at` + `destroy_at` + `deallocate` 는 C++20 상수 평가에서 허용된다 (placement new 는 불가). `ct_destroy<true>` 의 이중 소멸은 상수 평가 실패라, `std::bool_constant<(ct_destroy<Twice>(), true)>` 를 `requires` 안에서 쓰면 치환 실패로 `false` 가 된다. 런타임 코드의 수명 규칙을 컴파일 타임 테스트로 옮기는 기법이다. UB 탐지기를 공짜로 쓰는 셈이다.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 28/28 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer practice.cpp -o prac_san && ./prac_san   # 28/28, 보고 없음
```

## 10. 핵심 정리

- 지연 생성의 기본형: `alignas(T) std::byte s[sizeof(T)]{}` + `::new (static_cast<void*>(s)) T(args...)`. `{}` 가 없으면 `constinit` 불가, `::` 와 `void*` 는 이름 찾기 함정을 막는다.
- 포인터는 생성자가 **끝난 뒤** 채우고, 소멸자 **전에** 비운다. 생성 중 재진입은 상태로 막는다 (`ptr_` 만으로는 구분 불가).
- `launder`: `reinterpret_cast<T*>(storage)` 나 complete const / 베이스 서브객체 재생성 뒤의 옛 포인터에 필요. `new` 의 반환값을 저장하면 필요 없다. M4 명령 0개.
- 누수 판정은 주소 표와 `ctors == dtors` 를 함께 본다. 같은 주소 재생성은 주소 표만으로 놓친다.
- implicit-lifetime (POD) 은 `memcpy` 로 생기지만 드라이버는 placement new 가 필수. `start_lifetime_as` 는 이번 툴체인에 없음.
- over-aligned 는 `sizeof` 가 정렬 배수로 올라간다 (64 정렬이면 128). `.bss` 정렬은 ELF 에서 확인.
- ISR 공개: 생성 → 공개, 회수 → 소멸. 단일 코어는 `atomic` + `signal_fence` 면 되고, 멀티코어는 유예 기간이 필요.
- 펌웨어는 끝나지 않는다. 소멸자 없는 `constinit` 인스턴스는 text 40, bss 16 인데, 전역 소멸자를 달면 text 288, bss 424 로 불어난다 (실측).

## 참고자료

- C++20 표준: [basic.life], [intro.object], [basic.align], [basic.compound], [expr.new], [new.delete.placement], [ptr.launder], [basic.start.term], [class.prop], [cstring.syn], [atomics.fences] — 현재 초안 <https://eel.is/c++draft/basic.life>, <https://eel.is/c++draft/ptr.launder>, <https://eel.is/c++draft/intro.object>
- [cppreference — placement new](https://en.cppreference.com/w/cpp/language/new) · [std::launder](https://en.cppreference.com/w/cpp/utility/launder) · [std::construct_at](https://en.cppreference.com/w/cpp/memory/construct_at) · [std::destroy_at](https://en.cppreference.com/w/cpp/memory/destroy_at) · [std::atomic_signal_fence](https://en.cppreference.com/w/cpp/atomic/atomic_signal_fence)
- P0593R6 (implicit object creation) · P0960R3 (괄호 집합체 초기화) · P1331R2 (constexpr 미초기화 허용) · P2590R2 (`start_lifetime_as`) · P2674R1 (`is_implicit_lifetime`)
- ARM — *C++ ABI for the Arm Architecture* (`__aeabi_atexit`), ARMv7-M Architecture Reference Manual (DDI 0403) — A3.2 Alignment
- Clang 문서 — `-fstrict-vtable-pointers`
- 트랙 내 참고: CPP-B06/B07(RAII), CPP-B09(.init_array, static 초기화), CPP-B12(MMIO), CPP-B14(operator new 금지), CPP-A04(atomic), CPP-M08(pool), CPP-M10(static_vector)
