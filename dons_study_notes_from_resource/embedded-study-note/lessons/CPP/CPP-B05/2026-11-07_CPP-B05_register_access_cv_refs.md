---
id: CPP-B05
time: 50분
updated: 2026-11-07
---
# CPP-B05 const·volatile·참조로 레지스터 접근 모델링

## 학습 목표

- 레지스터 접근자가 `volatile const uint32_t&`(읽기 전용, RO) / `volatile uint32_t&`(읽기·쓰기, RW)를 돌려주게 만들어 **RO 레지스터에 쓰는 코드를 컴파일 에러**로 만든다.
- 참조만으로는 **쓰기 전용(WO)** 을 표현할 수 없다는 한계를 알고, 쓰기만 있는 작은 프록시(proxy)로 막는다.
- `requires` 식(requires-expression)을 **템플릿/concept 안에서** 써서 "이 코드는 컴파일되면 안 된다"를 `static_assert` 로 증명한다.
- `auto` 가 참조와 `volatile` 을 떨어뜨려 **폴링이 영원히 안 끝나는 스냅샷**을 만드는 함정을 어셈블리로 보여줄 수 있다.
- C++20의 `volatile` deprecation(P1152)과 C++23의 부분 해제(P2327)가 `REG |= bit`, `REG++` 에 무슨 영향을 주는지 clang 21에서 실제로 확인했다.

## 1. 문제

C 펌웨어에서 "읽기 전용 레지스터에 실수로 쓰기", "쓰기 전용 레지스터를 읽어서 쓰레기 값으로 판단하기"는 코드 리뷰로만 잡힌다. C++에서는 이걸 **타입으로 컴파일 타임에 막을 수 있다** — 그리고 면접관은 "그럼 `volatile` 은 뭘 보장하죠? 어셈블리로 보여줄 수 있어요?"까지 꼬리질문한다.

`volatile` 자체의 의미(생략·병합 금지, 원자성 아님)는 C 트랙에서 다뤘다 (C-B09 참고, MMIO 포인터/레지스터 구조체는 C-M01 참고). 이 레슨은 그 위에 **C++의 `const` · 참조 · 멤버 함수 const · concept** 을 얹어 "접근 권한"을 타입으로 만드는 데 집중한다.

모델로 쓰는 주변장치는 STM32G0/L4 계열 USART를 단순화한 mock 레지스터 블록이다 (실제 오프셋은 ISR 0x1C, ICR 0x20, RDR 0x24, TDR 0x28 이지만 여기선 0x00~0x10 으로 압축).

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">레지스터의 하드웨어 권한 → C++ 접근자 반환 타입</text>
  <g fill="currentColor" font-weight="bold">
    <text x="20" y="54">offset</text><text x="90" y="54">reg</text><text x="160" y="54">HW 권한</text>
    <text x="270" y="54">접근자</text><text x="400" y="54">반환 타입</text>
  </g>
  <line x1="10" y1="62" x2="730" y2="62" stroke="currentColor"/>
  <rect x="10" y="68" width="720" height="32" class="f-ok" opacity=".5"/>
  <rect x="10" y="100" width="720" height="32" class="f-acc" opacity=".5"/>
  <rect x="10" y="132" width="720" height="32" class="f-warn" opacity=".5"/>
  <rect x="10" y="164" width="720" height="32" class="f-acc" opacity=".5"/>
  <rect x="10" y="196" width="720" height="32" class="f-ok" opacity=".5"/>
  <g fill="currentColor" font-family="monospace">
    <text x="20" y="89">0x00</text><text x="90" y="89">CR1</text><text x="160" y="89">RW</text>
    <text x="270" y="89">cr1()</text><text x="400" y="89">volatile uint32_t&amp;</text>
    <text x="20" y="121">0x04</text><text x="90" y="121">ISR</text><text x="160" y="121">RO</text>
    <text x="270" y="121">isr() const</text><text x="400" y="121">volatile const uint32_t&amp;</text>
    <text x="20" y="153">0x08</text><text x="90" y="153">ICR</text><text x="160" y="153">WO (W1C)</text>
    <text x="270" y="153">icr()</text><text x="400" y="153">WoReg  (operator= 만)</text>
    <text x="20" y="185">0x0C</text><text x="90" y="185">RDR</text><text x="160" y="185">RO</text>
    <text x="270" y="185">rdr() const</text><text x="400" y="185">volatile const uint32_t&amp;</text>
    <text x="20" y="217">0x10</text><text x="90" y="217">TDR</text><text x="160" y="217">RW</text>
    <text x="270" y="217">tdr()</text><text x="400" y="217">volatile uint32_t&amp;</text>
  </g>
  <text x="10" y="252" fill="currentColor">RO 에 쓰기 → const 위반 컴파일 에러. WO 를 읽기 → 변환 연산자 없음 컴파일 에러.</text>
  <text x="10" y="272" fill="currentColor">const Usart&amp; 로는 RO 만 읽을 수 있고 RW/WO 쓰기는 불가 (멤버 함수 const 정책).</text>
</svg>
```

### Part A — 주소 → 참조 접근자

`reg_ro(base, offset)` 와 `reg_rw(base, offset)` 를 구현한다. 반환 타입은 별칭 `RoReg` / `RwReg` 로 정의한다.

- `RoReg` = 읽기 전용 레지스터 참조 (쓰려고 하면 컴파일 에러)
- `RwReg` = 읽기·쓰기 레지스터 참조
- 반환된 참조는 mock 메모리의 **바로 그 자리의 별칭(alias)** 이어야 한다 — 값 복사가 아니다. "하드웨어"(테스트 코드)가 값을 바꾸면 이미 잡아 둔 참조로도 새 값이 보여야 한다.
- 다 채운 뒤 파일 맨 아래 `static_assert` 주석을 풀어 **"RO 는 쓰기 불가"를 컴파일 타임 증명**으로 만든다.

### Part B — 드라이버 핸들의 const 정책과 쓰기 전용 레지스터

`Usart` 핸들 클래스의 접근자 시그니처를 고친다.

- `isr()`, `rdr()` 는 `const Usart&` 로도 호출 가능해야 한다 (상태 출력·로깅 코드는 const 핸들만 받는다).
- `cr1()` 은 `const Usart&` 로는 호출 불가여야 한다 — "const 핸들 = 읽기 전용 뷰" 정책.
- `ICR` 은 진짜 쓰기 전용(write-only)이다. `volatile uint32_t&` 를 주면 읽기도 되어버린다. 쓰기만 되는 `WoReg::operator=` 를 완성한다.
- 함정 확인: `auto snap = u.isr();` 와 `decltype(auto) alias = u.isr();` 의 차이를 테스트가 검사한다.

### Part C — 폴링 USART 드라이버

- `usart_enable(u)` — `CR1` 에 `UE|RE|TE` 를 켠다. 다른 비트(예: OVER8, bit 15)는 보존.
- `usart_wait(const Usart&, mask, max_spins)` — `ISR & mask` 가 0이 아닐 때까지 **최대 max_spins 번** 읽는다 (무한 대기 금지). 시간 초과면 `false`.
- `usart_put(u, byte, max_spins)` — TXE 를 기다렸다가 `TDR` 에 쓴다. 시간 초과면 **아무것도 쓰지 않고** `false`.
- `usart_get(u)` — `ISR` 을 **한 번만** 읽어 판단한다. ORE(overrun)면 `ICR = ORECF` 로 지우고, RXNE 면 `RDR` 하위 8비트, 아니면 `-1`.

## 2. Problem (English)

> **Type-safe memory-mapped register access.**
>
> A peripheral exposes read-only (`ISR`, `RDR`), read-write (`CR1`, `TDR`), and write-only (`ICR`, write-1-to-clear) 32-bit registers at `base + offset`.
>
> 1. Implement `RoReg reg_ro(uintptr_t base, uintptr_t off)` and `RwReg reg_rw(...)` where `RoReg = volatile const uint32_t&` and `RwReg = volatile uint32_t&`. The returned references must alias the register (not copy it). Prove at compile time (`static_assert` + a concept with a `requires` expression) that assigning through `RoReg` is ill-formed.
> 2. Write a `Usart` handle with accessors `isr() const`, `rdr() const`, `cr1()`, `tdr()`, `icr()`. A `const Usart&` must allow reading status but must not allow writing any register. `icr()` must be write-only: reading it must not compile.
> 3. Using only those accessors, implement `usart_enable`, a bounded `usart_wait(const Usart&, mask, max_spins)`, `usart_put`, and `usart_get` (sample `ISR` once; clear overrun via `ICR`).
> 4. Show with `-O2` assembly that a polling loop on a `volatile` read keeps the load inside the loop while a non-volatile version hoists it (or deletes the loop). Which of `REG |= m;`, `REG++;` does `-std=c++20` warn about, and why?
>
> Example: with `ISR = RXNE|ORE` and `RDR = 0x42`, `usart_get(u) == 0x42` and `ICR == ORECF` afterwards.

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 회피
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
# 처음엔 FAIL 이 18줄, "6/24 passed" — TODO 를 채워 24/24 를 만든다
```

starter 는 컴파일은 된다. 대신 `RoReg` 가 일부러 쓰기 가능한 타입이라 `CHECK(!WritableReg<RoReg>)` 같은 **컴파일타임 판정이 런타임 FAIL 로 보고**된다. 다 고친 뒤 맨 아래 `static_assert` 두 줄의 주석을 풀면 같은 판정이 "컴파일이 안 되는" 수준으로 올라간다.

## 4. 힌트

<details>
<summary>힌트 1 — 주소를 참조로 바꾸기</summary>

C에서 `*(volatile uint32_t *)(base + off)` 하던 것과 똑같다. `reinterpret_cast<volatile const std::uint32_t*>(base + offset)` 로 포인터를 만들고 `*` 로 역참조하면 그 lvalue 에 참조가 묶인다. 반환 타입이 참조이므로 복사는 일어나지 않는다.

</details>

<details>
<summary>힌트 2 — "컴파일되면 안 된다"를 bool 로 바꾸는 법</summary>

`template <class R> concept WritableReg = requires(R r) { r = 1u; };` — 템플릿 인자를 넣었을 때 `r = 1u` 가 ill-formed 면 concept 이 **false** 가 된다. 템플릿 밖에서 `requires` 를 쓰면 false 가 아니라 그냥 컴파일 에러다 (7. 자주 하는 실수).

</details>

<details>
<summary>힌트 3 — const 핸들 정책은 멤버 함수 뒤의 const</summary>

`RoReg isr() const` 는 `const Usart&` 로 호출 가능. `RwReg cr1()` (const 없음)은 `const Usart&` 로 호출하면 "`this` argument ... not marked const" 에러. `base_` 는 정수라서 컴파일러는 `cr1() const` 도 허용한다 — **막는 건 우리의 설계 선택**이다.

</details>

<details>
<summary>힌트 4 — 쓰기 전용 프록시</summary>

`volatile uint32_t&` 를 멤버로 들고, `void operator=(std::uint32_t v) const` 만 제공한다. `operator std::uint32_t()` 를 **만들지 않으면** 읽기가 불가능해진다. `|=` 도 정의하지 않았으니 RMW 도 막힌다 (W1C 레지스터에 딱 맞는 성질).

</details>

<details>
<summary>힌트 5 — usart_get 은 ISR 을 몇 번 읽어야 하나</summary>

`if (u.isr() & ORE) ...; if (u.isr() & RXNE) ...` 는 `volatile` 이라 **실제로 두 번** 읽는다. 그 사이 하드웨어가 비트를 바꾸면 두 판단이 서로 다른 시점을 본다. 한 번 읽어 `const std::uint32_t isr = u.isr();` 지역 변수(스냅샷)로 판단한다 — 여기서는 스냅샷이 *의도* 다.

</details>

## 5. 풀이

### Part A — cv 를 "더하는" 건 공짜, "빼는" 건 에러

참조 바인딩 규칙([dcl.init.ref])은 "참조 쪽 cv 가 대상의 cv 이상"일 때만 허용한다 (reference-compatible). 그래서 아래 사다리에서 **오른쪽(더 제한)으로는 자동 변환**, 왼쪽으로는 에러다.

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">참조의 cv 사다리 — 권한은 줄일 수만 있다</text>
  <g stroke="currentColor" fill="none">
    <rect x="15" y="50" width="160" height="44" rx="6"/>
    <rect x="290" y="50" width="185" height="44" rx="6"/>
  </g>
  <rect x="560" y="50" width="170" height="44" rx="6" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="95" y="77">uint32_t&amp;</text>
    <text x="382" y="77">volatile uint32_t&amp;</text>
    <text x="645" y="70">volatile const</text><text x="645" y="86">uint32_t&amp;</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M178 66 L284 66"/><path d="M276 61 L286 66 L276 71"/>
    <path d="M478 66 L554 66"/><path d="M546 61 L556 66 L546 71"/>
    <path d="M284 84 L178 84" stroke-dasharray="5 4"/>
    <path d="M554 84 L478 84" stroke-dasharray="5 4"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="231" y="58" font-size="12">OK</text><text x="516" y="58" font-size="12">OK</text>
    <text x="231" y="104" font-size="12">에러: drops volatile</text><text x="516" y="104" font-size="12">에러</text>
  </g>
  <g fill="currentColor">
    <text x="15" y="140">일반 변수 (캐시 가능)</text>
    <text x="290" y="140">RW 레지스터 (매번 load/store)</text>
    <text x="560" y="140">RO 레지스터 (읽기만)</text>
    <text x="15" y="180">→ 폴링 함수 매개변수는 가장 오른쪽 const volatile&amp; 로 받으면 셋 다 받는다 (연습 2).</text>
    <text x="15" y="205">← 거꾸로 가는 유일한 길은 const_cast — 컴파일은 되지만 설계 의도를 깨는 탈출구.</text>
  </g>
</svg>
```

정답 핵심:

```cpp
using RoReg = volatile const std::uint32_t&;   // const volatile 와 같은 타입 (cv 순서 무관)
using RwReg = volatile std::uint32_t&;

inline RoReg reg_ro(std::uintptr_t base, std::uintptr_t offset) {
    return *reinterpret_cast<volatile const std::uint32_t*>(base + offset);
}
```

| 식 | `RoReg r` | `RwReg r` | 근거 |
|---|---|---|---|
| `std::uint32_t v = r;` | OK (volatile load 1회) | OK | lvalue-to-rvalue 변환 |
| `r = 1u;` | **에러** | OK | const 객체에 대입 불가 ([expr.ass]: 왼쪽은 수정 가능한 lvalue) |
| `r \|= 1u;` | **에러** | OK (경고 없음, 아래 Part C) | 같은 이유 |
| `std::uint32_t& p = r;` | 에러 | **에러** | volatile 을 떨어뜨리는 바인딩 |
| `auto a = r;` | `uint32_t` 복사본 | `uint32_t` 복사본 | auto 는 참조·top-level cv 제거 |

**컴파일타임 증명.** `WritableReg<RoReg>` 는 `requires(R r) { r = 1u; }` 를 `R = volatile const uint32_t&` 로 치환해 보는데, 대입이 ill-formed 라 **false**. 템플릿(concept) 치환 실패는 에러가 아니라 false 로 떨어진다 ([expr.prim.req], [temp.constr]). 그래서 `static_assert(!WritableReg<RoReg>)` 가 "이 코드는 컴파일되면 안 된다"의 증명이 된다. 실제 에러 메시지는 데모로 확인했다 (Apple clang 21, `-DSHOW_ERROR`, 메모 줄 일부 생략):

```text
$ c++ -std=c++20 -fsyntax-only -DSHOW_ERROR demo_ro_error.cpp
demo_ro_error.cpp:35:13: error: cannot assign to return value because function 'isr' returns a const value
demo_ro_error.cpp:21:5: note: function 'isr' which returns const-qualified type 'RoReg' (aka 'const volatile unsigned int &') declared here
demo_ro_error.cpp:36:13: error: cannot assign to return value because function 'isr' returns a const value
demo_ro_error.cpp:37:20: error: binding reference of type 'std::uint32_t' (aka 'unsigned int') to value of type 'volatile std::uint32_t' (aka 'volatile unsigned int') drops 'volatile' qualifier
demo_ro_error.cpp:38:5: error: 'this' argument to member function 'cr1' has type 'const Usart', but function is not marked const
demo_ro_error.cpp:39:19: error: no viable conversion from 'WoReg' to 'std::uint32_t' (aka 'unsigned int')
demo_ro_error.cpp:40:13: error: no viable overloaded '|='
demo_ro_error.cpp:41:49: error: cannot assign to return value because function 'isr' returns a const value
7 errors generated.
```

35~41행이 데모의 (1)~(7) 이다: RO 쓰기, RO `|=`, volatile 떨어뜨리는 바인딩, const 핸들로 RW 쓰기, WO 읽기, WO `|=`, 템플릿 밖 `requires`. 마지막 (7)은 false 가 아니라 **하드 에러**가 된다는 증거다.

`demo_ro_error.cpp` 의 정상 빌드(`-DSHOW_ERROR` 없이)는 `const_cast<volatile std::uint32_t&>(u.isr()) = 0xDEAD;` 가 **컴파일되고 실행된다**는 것도 보여준다 (`mem0=0xDEAD`). 타입은 실수를 막는 가드레일이지 악의를 막는 벽이 아니다. 실제 칩에서는 RO 레지스터 쓰기가 무시되거나(대부분의 STM32) 버스 폴트가 날 수 있다.

**C와 비교.** C에서는 CMSIS 의 `__IM`(= `volatile const`)·`__IOM`(= `volatile`) 구조체 멤버로 같은 일을 한다. 그런데 CMSIS core 헤더(core_cm4.h 등)는 `__I` 를 C에서는 `volatile const`, **C++에서는 `volatile` 로만** 정의하고, `__O`(쓰기 전용)는 C/C++ 모두 그냥 `volatile` 이다 — 즉 기존 헤더로는 C++에서 RO/WO 가 타입으로 강제되지 않는 경우가 있다. 이 레슨의 접근자 계층이 그 구멍을 메운다. 비용: 참조 반환 inline 함수는 `-O2` 에서 C 매크로와 똑같은 `ldr/str` 한 개로 접힌다 (아래 `usart_wait` 어셈블리).

> 참고: `reinterpret_cast` 가 들어가므로 `reg_ro` 는 `constexpr` 가 될 수 없다 ([expr.const] 는 상수 평가 중 `reinterpret_cast` 를 금지). 주소·마스크 *값* 계산은 constexpr(CPP-B04 참고), 실제 *접근* 은 런타임 — 이 경계가 정상이다. 폭(8/16/32)별 일반화는 CPP-B12, 주소까지 타입에 넣는 `Reg<Addr, T, Access>` 는 CPP-M01 에서 다룬다.

### Part B — 멤버 함수 const 와 쓰기 전용 프록시

**const 정책.** `Usart` 는 `base_` 정수 하나만 들고 있다. `cr1() const` 로 선언해도 `base_` 를 바꾸지 않으므로 컴파일러는 허용한다 — C++의 const 는 **얕다(shallow)**: 핸들 객체의 비트만 지키고, 그 핸들이 가리키는 하드웨어는 모른다. 그래서 "const 핸들 = 하드웨어를 바꿀 수 없는 뷰"라는 **논리적 const(logical constness)** 를 우리가 시그니처로 정해준다.

| 접근자 | 시그니처 | `Usart&` | `const Usart&` |
|---|---|---|---|
| 상태 읽기 | `RoReg isr() const` | 읽기 | 읽기 |
| 수신 데이터 | `RoReg rdr() const` | 읽기 | 읽기 |
| 제어 | `RwReg cr1()` | 읽기/쓰기 | **호출 불가** |
| 송신 데이터 | `RwReg tdr()` | 읽기/쓰기 | 호출 불가 |
| 플래그 지우기 | `WoReg icr()` | 쓰기만 | 호출 불가 |

이렇게 하면 `void log_status(const Usart&)` 같은 함수는 **시그니처만 보고도** 레지스터를 안 건드린다는 걸 리뷰어가 안다. `usart_wait` 도 `const Usart&` 를 받는다.

**쓰기 전용(WO).** C++ 참조 타입에는 "쓰기만" 이라는 한정자가 없다. `volatile uint32_t&` 는 읽기도 된다. WO 레지스터를 읽으면 칩마다 0, 마지막 쓴 값, 또는 정의되지 않은 값이 나온다 — 그걸 믿고 RMW(`ICR |= x`) 하면 버그다. 해결은 쓰기만 있는 프록시:

```cpp
class WoReg {
public:
    explicit WoReg(volatile std::uint32_t& r) : r_(r) {}
    void operator=(std::uint32_t v) const { r_ = v; }   // 쓰기만. 반환 void → a = b = c 도 막힘
private:
    volatile std::uint32_t& r_;                          // 변환 연산자 없음 → 읽기 불가
};
```

`sizeof(WoReg) == sizeof(void*)` 이고 inline 되면 그냥 `str` 하나다 (연습 4). 참조 멤버 때문에 복사 대입은 암시적으로 delete 되는데, 프록시 용도엔 오히려 맞다.

**`auto` 함정.** 테스트의 이 부분이 핵심 교훈이다.

```cpp
auto           snap  = u.isr();   // 타입: std::uint32_t — 지금 값의 복사본
decltype(auto) alias = u.isr();   // 타입: const volatile std::uint32_t& — 레지스터 별칭
```

`auto` 는 템플릿 인자 추론과 같은 규칙이라 참조와 top-level cv 를 버린다 ([dcl.type.auto.deduct], [temp.deduct.call]). 폴링 루프에서 `auto sr = u.isr(); while (!(sr & TXE)) {}` 라고 쓰면 **`volatile` 이 사라진 지역 변수**를 도는 것이다. 다음 절 어셈블리에서 그 결과를 본다.

### Part C — 드라이버, 그리고 어셈블리로 보는 volatile

```cpp
void usart_enable(Usart& u) { u.cr1() |= bit::CR1_UE | bit::CR1_RE | bit::CR1_TE; }

bool usart_wait(const Usart& u, std::uint32_t mask, std::uint32_t max_spins) {
    for (std::uint32_t n = 0; n < max_spins; ++n)
        if (u.isr() & mask) return true;      // 매 반복 실제 load
    return false;
}

int usart_get(Usart& u) {
    const std::uint32_t isr = u.isr();        // 의도된 스냅샷: 한 시점의 상태로 판단
    if (isr & bit::ISR_ORE) u.icr() = bit::ICR_ORECF;   // W1C: = 로 쓴다, |= 아님
    if (!(isr & bit::ISR_RXNE)) return -1;
    return static_cast<int>(u.rdr() & 0xFFu);
}
```

- `usart_wait` 는 **유한 루프**다. 실제 칩에서 클럭이 안 켜졌거나 핀이 잘못 설정되면 TXE 가 영원히 안 올 수 있다. 펌웨어 면접에서 `while (!(SR & TXE));` 만 쓰면 "타임아웃은요?" 가 바로 나온다.
- `usart_get` 의 ORE 처리: ICR 은 write-1-to-clear 라 **지울 비트만 1로** 쓴다. `WoReg` 에는 `|=` 가 없으니 실수로 `u.icr() |= ...` 라고 쓰면 컴파일 에러다 (C-B01 연습 6 의 W1C 함정을 타입이 막아준다).
- 테스트의 `RDR = 0x141` 은 9비트 데이터 모드 잔재 비트가 섞인 상황: `& 0xFF` 로 하위 8비트만 돌려준다.

**어셈블리 검증.** `demo_poll_asm.cpp` 를 `-O2` 로 컴파일한 실제 출력이다 (Apple clang 21, arm64, `.cfi`/`Lloh` 지시어 제거):

```bash
c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -S -o - demo_poll_asm.cpp | grep -v '^\s*\.'
```

```text
__Z10wait_plainv:                 ; while (!(plain_sr & 0x80u)) {}
	ret                            ; ← 루프가 통째로 사라졌다
__Z13wait_volatilev:              ; while (!(vol_sr & 0x80u)) {}
	adrp	x8, _vol_sr@GOTPAGE
	ldr	x8, [x8, _vol_sr@GOTPAGEOFF]
LBB1_1:
	ldr	w9, [x8]                  ; ← 매 반복 load
	tbz	w9, #7, LBB1_1
	ret
```

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">spin_plain vs spin_volatile (타임아웃 폴링, 실제 -O2 arm64 출력 요약)</text>
  <rect x="10" y="38" width="350" height="215" rx="8" stroke="currentColor" fill="none"/>
  <rect x="380" y="38" width="350" height="215" rx="8" stroke="currentColor" fill="none"/>
  <text x="22" y="60" fill="currentColor" font-weight="bold">plain_sr (non-volatile)</text>
  <text x="392" y="60" fill="currentColor" font-weight="bold">vol_sr (volatile)</text>
  <rect x="20" y="70" width="330" height="24" class="f-warn"/>
  <rect x="20" y="104" width="330" height="110" class="f-mut"/>
  <rect x="390" y="104" width="330" height="110" class="f-mut"/>
  <rect x="396" y="134" width="318" height="24" class="f-ok"/>
  <g fill="currentColor" font-family="monospace">
    <text x="28" y="87">ldr  w9, [x8]      ; 루프 밖 1회</text>
    <text x="28" y="124">LBB2_1:</text>
    <text x="28" y="148">  add  w8, w8, #1</text>
    <text x="28" y="172">  tbnz w9, #7, done ; 같은 w9</text>
    <text x="28" y="196">  cmp w8,w0; b.lo LBB2_1</text>
    <text x="398" y="124">LBB3_1:</text>
    <text x="398" y="151">  ldr  w10, [x9]   ; 매 반복</text>
    <text x="398" y="176">  tbnz w10, #7, done</text>
    <text x="398" y="200">  cmp w0,w8; b.lo LBB3_1</text>
  </g>
  <text x="22" y="240" fill="currentColor">비트가 처음에 0이면 max 번 돌고 끝 — 절대 못 봄</text>
  <text x="392" y="240" fill="currentColor">하드웨어가 비트를 켜는 순간 다음 반복에서 봄</text>
  <text x="10" y="282" fill="currentColor">auto sr = isr(); 로 받은 spin_auto_copy 도 왼쪽과 똑같이 load 가 루프 밖으로 나갔다 (아래 표).</text>
</svg>
```

같은 파일의 네 함수 결과 정리 (모두 실제 출력에서 확인):

| 함수 | 소스 | `-O2` 결과 |
|---|---|---|
| `wait_plain` | 무한 폴링, non-volatile | **`ret` 하나** — 루프 삭제 |
| `wait_volatile` | 무한 폴링, volatile | `ldr` 이 루프 안 |
| `spin_plain` | 타임아웃 폴링, non-volatile | `ldr` 이 루프 **밖**(hoist), 루프는 카운터만 |
| `spin_volatile` | 타임아웃 폴링, volatile | `ldr w10, [x9]` 루프 안 |
| `spin_ref` | `const volatile uint32_t& sr = isr();` | `ldr w8, [x0]` 루프 안 |
| `spin_auto_copy` | `auto sr = isr();` | `bl isr` 직후 `ldr` 1회, 루프 밖 |

왜 `wait_plain` 은 아예 사라졌나? C++ **전진 보장(forward progress)** 규칙 [intro.progress] 때문이다: 구현은 모든 스레드가 결국 ① 종료하거나 ② I/O 라이브러리 호출 ③ **volatile 접근** ④ 동기화/atomic 연산 중 하나를 한다고 가정해도 된다. `wait_plain` 의 루프는 그 어떤 것도 안 하므로 "언젠가 끝난다"고 가정 → 끝난 뒤 상태가 같으니 루프를 지운다. volatile 접근은 ③에 해당하므로 `wait_volatile` 은 살아남는다. 같은 코드를 C(`cc -std=c11 -O2`)로 컴파일하면 clang 은 루프를 지우지 않고 `ldrb` 1회 후 `b LBB0_1`(자기 자신으로 점프하는 무한 루프)을 만들었다 — 어느 쪽이든 하드웨어 대기로는 **버그**다. (C++26 P2809 는 `while (true) {}` 같은 *상수 조건* 의 빈 무한 루프만 UB 에서 구제한다 — 이 경우와 무관.)

C 버전의 `ldrb`(바이트 load)도 눈여겨볼 점: non-volatile 이면 컴파일러가 bit 7 만 필요하다고 보고 접근 폭을 1바이트로 줄였다. 일부 주변장치는 32비트 외 접근에 버스 폴트를 내거나 읽기 부수효과가 있다. volatile 버전은 32비트 `ldr w9` 를 유지했다 (표준이 폭을 보장하진 않지만 GCC/clang 은 volatile 접근 폭을 유지한다).

실제 드라이버 `usart_wait` 의 출력도 같다 — `base_` 로드(`ldr x8, [x0]`)는 non-volatile 이라 루프 밖으로 hoist 되고, ISR 로드(`ldr w10, [x8, #4]`, 오프셋 4 = ISR)만 루프 안에 남는다. 참조 반환 접근자의 추상화 비용은 0이다.

> **Cortex-M 예상 (직접 실행 못함):** `arm-none-eabi-g++ -O2 -mcpu=cortex-m4 -mthumb` 이면 `wait_volatile` 은 대략 `ldr r2, =vol_sr` (리터럴 풀) → `.L: ldr r3, [r2]; lsls r3, r3, #24; bpl .L; bx lr` 형태로, 역시 `ldr` 이 루프 안에 있을 것으로 예상한다. 실제 MMIO 주소(예: `0x4000441C`)라면 그 상수가 리터럴 풀에 들어간다. GCC 는 non-volatile 무한 루프를 버전·옵션(`-ffinite-loops`, C++11 이상 -O2 기본)에 따라 지우거나 `b .` 로 만든다 — 어느 쪽이든 버그.

**C++20 volatile deprecation — clang 21 이 실제로 경고하는 것.** P1152R4(C++20)는 "의미가 애매한" volatile 사용을 deprecated 로 만들었다: `++`/`--`, compound 대입(`+=`, `|=` …), volatile 대입식의 결과값 사용, volatile 값 매개변수/반환 타입. 그런데 임베디드 코드 전체가 `REG |= bit` 로 되어 있어서 반발이 컸고, **P2327R1(C++23)이 비트 compound(`|=` `&=` `^=`)만 다시 허용**했다. `demo_deprecated_volatile.cpp` 실제 결과:

```text
$ c++ -std=c++17 -Wall -Wextra -fsyntax-only demo_deprecated_volatile.cpp
(경고 없음)
$ c++ -std=c++20 -Wall -Wextra -fsyntax-only demo_deprecated_volatile.cpp
demo_deprecated_volatile.cpp:13:8: warning: increment of object of volatile-qualified type 'volatile std::uint32_t' (aka 'volatile unsigned int') is deprecated [-Wdeprecated-volatile]
demo_deprecated_volatile.cpp:14:5: warning: decrement of object of volatile-qualified type ... is deprecated [-Wdeprecated-volatile]
demo_deprecated_volatile.cpp:15:24: warning: use of result of assignment to object of volatile-qualified type ... is deprecated [-Wdeprecated-volatile]
demo_deprecated_volatile.cpp:19:35: warning: volatile-qualified parameter type 'volatile std::uint32_t' (aka 'volatile unsigned int') is deprecated [-Wdeprecated-volatile]
4 warnings generated.
$ c++ -std=c++23 ...   → 위와 동일한 4개
```

| 코드 | 표준 C++20 | 표준 C++23 | clang 21 `-std=c++20` |
|---|---|---|---|
| `REG \|= m;` `&=` `^=` | deprecated | **허용** (P2327) | 경고 없음 |
| `REG += 1;` `<<=` | deprecated | 여전히 deprecated | **경고 없음** (관찰) |
| `REG++;` `--REG;` | deprecated | deprecated | 경고 |
| `x = (REG = 5);` | deprecated | deprecated | 경고 |
| `void f(volatile uint32_t)` | deprecated | deprecated | 경고 |
| `REG = REG + 1;` | OK | OK | 경고 없음 |

정리: **`REG |= bit` 는 C++20 모드 clang 에서 경고가 안 나므로 그대로 써도 된다** (P2327 이 C++20 모드에도 적용된 상태). 반면 `-Wall -Werror` 빌드에서 `REG++` 은 **빌드 실패**다 (4 errors 확인). 산술 compound(`+=`)는 표준상 여전히 deprecated 인데 clang 21 은 진단하지 않는다 — GCC 는 `-Wvolatile`(C++20 기본 켜짐)로 경고하는 것으로 알려져 있으나 이 맥에선 확인하지 못했다. 이식성을 위해 **카운터는 `REG = REG + 1;` 처럼 읽기 1번·쓰기 1번이 보이게** 쓰자 (연습 3). 이 deprecation 의 취지 자체가 "`REG++` 이 원자적인 것처럼 보이지만 RMW 3단계"라는 착각을 없애는 것이다 (ISR 경쟁은 C-M14, CPP-B06 참고).

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution   # 24/24 passed
c++ -std=c++20 -g -O1 -fsanitize=address,undefined solution.cpp -o sol_san && ./sol_san                       # 24/24, 경고 없음

# 데모 (레슨 폴더에 있음)
c++ -std=c++20 -Wall -Wextra -Werror -fno-exceptions -fno-rtti demo_ro_error.cpp -o d && ./d   # isr=0 cr1=1 icr=8 mem0=0xDEAD
c++ -std=c++20 -fsyntax-only -DSHOW_ERROR demo_ro_error.cpp                                     # 7 errors
c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -S -o - demo_poll_asm.cpp | grep -v '^\s*\.'       # 위 어셈블리
c++ -std=c++20 -Wall -Wextra -fsyntax-only demo_deprecated_volatile.cpp                         # 4 warnings
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `uint32_t read_isr()` 처럼 **값**으로 반환 | 호출 시점 복사본. 호출자가 그걸 루프에서 재사용하면 volatile 이 없어 hoist 된다 | `volatile const uint32_t& isr() const` |
| `auto sr = u.isr(); while (!(sr & TXE)) {}` | `auto` 가 참조·volatile 제거 → `spin_auto_copy` 처럼 load 1회 | `const volatile auto& sr = u.isr();` 또는 매번 `u.isr()` |
| `static_assert(!requires(Usart& u){ u.isr() = 1u; });` (템플릿 밖) | 템플릿 밖 requires 의 ill-formed 식은 false 가 아니라 **하드 에러** ([expr.prim.req]) — demo (7) 이 실제로 에러 | concept/변수 템플릿으로 감싸기: `template<class U> concept IsrWritable = requires(U& u){...};` |
| WO 레지스터를 `volatile uint32_t&` 로 노출 | 읽기가 허용됨 → `ICR \|= flag` 같은 RMW 가 컴파일됨 | `WoReg` 프록시 (operator= 만) |
| W1C 레지스터에 `\|=` | 현재 1인 비트를 전부 다시 써서 모두 지움 (C-B01 연습 6) | `u.icr() = FLAG;` |
| `cr1() const` 로 선언 | 컴파일은 되지만 const 핸들로 하드웨어 변경 가능 → 시그니처가 거짓말 | 쓰기 접근자는 non-const |
| `REG++` 로 이벤트 카운트 | C++20 `-Wdeprecated-volatile` → `-Werror` 빌드 실패, 그리고 원자적이지도 않음 | `REG = REG + 1;` (+ ISR 공유면 임계 구역) |
| volatile 로 정의된 객체를 `const_cast` 로 non-volatile 접근 | volatile 객체를 non-volatile glvalue 로 접근하면 **UB** ([dcl.type.cv]) | cv 는 더하기만, 빼지 않는다 |
| `while (!(u.isr() & TXE));` 무한 대기 | 하드웨어 고장·설정 실수 시 영원히 멈춤 (워치독 리셋 루프) | `usart_wait(u, TXE, max_spins)` 로 상한 |

## 8. 면접 Q&A

<details>
<summary>Q1. 읽기 전용 상태 레지스터를 C++ 타입으로 어떻게 표현하나요? 비용은요?</summary>

접근자가 `volatile const uint32_t&` 를 반환하게 한다. `const` 는 쓰기를 컴파일 에러로, `volatile` 은 매 접근이 실제 load 가 되게 한다. 참조 반환 inline 함수라 `-O2` 에서 `ldr` 하나 — C 매크로 `*(volatile const uint32_t*)ADDR` 와 동일 코드. 꼬리질문 "쓰기 전용은?" → 참조로는 불가, `operator=` 만 있는 프록시. "더 일반화하면?" → `Reg<Addr, T, Access>` 템플릿 (CPP-M01).

</details>

<details>
<summary>Q2. volatile 이 빠지면 폴링 루프에서 정확히 무슨 일이 생기나요? 증명해 보세요.</summary>

`-O2 -S` 로 보면 non-volatile 은 load 가 루프 밖으로 hoist 되고(`spin_plain`), 탈출 조건이 없는 빈 루프는 C++ forward-progress 규칙 때문에 **통째로 삭제**된다(`wait_plain` → `ret`). volatile 은 [intro.abstract] 의 관측 가능한 동작(observable behavior)이고 [intro.progress] 의 "진행"으로도 인정되어 `ldr` 이 루프 안에 남는다. 꼬리질문 "그럼 volatile 이면 ISR 과 공유 변수 안전?" → 아니다, 원자성·순서는 별개 (C-A02, CPP-A04).

</details>

<details>
<summary>Q3. C++20 에서 volatile 이 deprecated 됐다던데, `GPIOA->ODR |= (1u << 5);` 는 이제 못 쓰나요?</summary>

P1152(C++20)가 volatile 의 compound 대입·`++`/`--`·대입 결과 사용·volatile 매개변수를 deprecated 했지만, 임베디드 반발로 P2327(C++23)이 `|=` `&=` `^=` 는 되살렸다. clang 21 은 `-std=c++20` 에서도 `|=` 에 경고하지 않는다(직접 확인). `REG++` 은 경고 → `-Werror` 면 빌드 실패. 대안은 `REG = REG + 1;`. 꼬리질문 "왜 deprecated 했나?" → `++` 가 한 번의 원자적 연산처럼 *보이지만* load-modify-store 3단계라 오해를 부르기 때문.

</details>

<details>
<summary>Q4. `const volatile` 은 모순 아닌가요? 둘이 같이 붙는 실제 예는?</summary>

모순 아니다. `const` = "**이 코드는** 쓰지 않는다", `volatile` = "**다른 누군가**(하드웨어, ISR)가 바꿀 수 있으니 매번 읽어라". 상태 레지스터, 수신 데이터 레지스터, 하드웨어 타이머 카운터 읽기 전용 뷰가 정확히 그 예. 꼬리질문 "const 면 컴파일러가 상수로 접어도 되지 않나?" → volatile 이 붙으면 접을 수 없다. `const` 만 있는 전역은 접히지만 `const volatile` 은 매번 load.

</details>

<details>
<summary>Q5. `auto x = reg();` 와 `decltype(auto) x = reg();` 는 어떻게 다른가요? 왜 중요하죠?</summary>

`auto` 는 템플릿 추론 규칙이라 참조와 top-level cv 를 버려 `uint32_t` 복사본이 된다. `decltype(auto)` 는 `decltype(reg())` = `const volatile uint32_t&` 를 그대로 쓴다. 레지스터 별칭을 지역 이름으로 잡을 때 `auto` 를 쓰면 volatile 이 조용히 사라져 폴링이 hoist 된다 (`spin_auto_copy`). 로깅/트레이스 래퍼도 `decltype(auto)` 로 반환해야 참조가 보존된다 (연습 5).

</details>

<details>
<summary>Q6. const 멤버 함수가 volatile uint32_t& 를 반환해도 컴파일되는데, 왜 굳이 non-const 로 두나요?</summary>

C++ const 는 얕아서 핸들 객체(`base_` 정수)만 보호한다. 하드웨어 상태는 핸들 밖이니 컴파일러는 상관 안 한다. 그래서 "const 핸들은 하드웨어를 못 바꾼다"는 **논리적 const** 를 시그니처로 정한다. 효과: `void dump(const Usart&)` 만 보고 부수효과 없음을 알 수 있고, 실수로 쓰면 컴파일 에러. `std::span<T>` 의 const 가 원소로 전파되지 않는 것과 같은 얕은 const 이야기.

</details>

<details>
<summary>Q7. "이 코드는 컴파일되면 안 된다"를 단위 테스트로 어떻게 남기나요?</summary>

concept/변수 템플릿 안의 requires-expression 으로 bool 을 만들고 `static_assert(!IsrWritable<Usart>)` 한다. 치환 실패는 false 가 되므로 테스트가 "컴파일 실패"를 값으로 검사한다. 템플릿 밖에 바로 `requires` 를 쓰면 하드 에러라 안 된다. 보조로 `-DSHOW_ERROR` 데모를 CI 에서 "컴파일이 실패해야 성공"으로 돌리기도 한다 (CMake `try_compile`, `WILL_FAIL`). C++17 이전이라면 SFINAE + `std::void_t` detection idiom.

</details>

## 9. 추가 연습

### 연습 1 — cv 포인터 4종 분류

`volatile uint32_t*`, `uint32_t* volatile`, `volatile uint32_t* const`, `const volatile uint32_t* const` 각각에서 "가리키는 값"과 "포인터 자체" 중 무엇이 volatile/const 인지 type trait 으로 판정하라.

<details>
<summary>답 보기</summary>

`*` 왼쪽의 cv 는 **가리키는 값**, 오른쪽은 **포인터 변수 자체**에 붙는다. `std::is_volatile_v<std::remove_pointer_t<P>>` 가 pointee, `std::is_volatile_v<P>` 가 포인터 자체. MMIO 는 `volatile uint32_t*`(pointee volatile)가 정답이고, `uint32_t* volatile` 은 ISR 이 바꾸는 *포인터 변수*(예: DMA 다음 버퍼 포인터)에 쓴다. 고정 주소 RO 레지스터는 `const volatile uint32_t* const`.

</details>

### 연습 2 — 무엇이든 받는 폴링 함수

`bool wait_bits(const volatile uint32_t& reg, uint32_t mask, uint32_t max_spins)` 를 만들고 RW 참조, RO 참조, 일반 `uint32_t` 변수를 모두 넘겨 보라.

<details>
<summary>답 보기</summary>

`const volatile T&` 는 cv 사다리의 맨 오른쪽이라 `T&`, `volatile T&`, `const volatile T&` 모두 cv 를 **더하는** 변환으로 바인딩된다. 폴링 헬퍼는 "읽기만 하고 매번 읽는다"는 계약이므로 이 타입이 정확하다. 일반 변수를 넘겨도 함수 안에선 volatile 로 읽힌다 (호스트 단위 테스트에 편리).

</details>

### 연습 3 — 경고 없는 RMW 헬퍼

`set_bits`, `clear_bits`, `bump`(카운터 +1)를 `volatile uint32_t&` 로 받아 `-std=c++20 -Wall -Werror` 로 빌드되게 작성하라.

<details>
<summary>답 보기</summary>

`r |= m;` `r &= ~m;` 는 P2327 이후 허용(clang 21 경고 없음). `bump` 는 `c++` 대신 `c = c + 1u;` — 읽기 1번, 쓰기 1번이 코드에 보인다. practice.cpp 가 `-Werror` 로 컴파일되는 것 자체가 검증이다. 원자성이 필요하면 별도로 임계 구역(CPP-B06)이나 `std::atomic`(CPP-A04).

</details>

### 연습 4 — 쓰기 전용 프록시의 비용

`WoReg` 의 `sizeof`, 읽기/쓰기 가능 여부, 복사 대입 가능 여부를 static 판정으로 확인하라.

<details>
<summary>답 보기</summary>

`sizeof(WoReg) == sizeof(void*)` — 참조 멤버 하나는 구현상 포인터 하나. `WritableReg<WoReg>` 참, `ReadableReg<WoReg>` 거짓. 참조 멤버 때문에 `std::is_copy_assignable_v<WoReg>` 는 거짓(암시적 복사 대입 delete). inline 되면 `str` 한 개라 런타임 비용 0.

</details>

### 연습 5 — 래퍼가 참조를 보존하는가

레지스터 접근을 트레이스하는 래퍼 `traced(f)` 를 `auto` 반환과 `decltype(auto)` 반환 두 가지로 만들고, 하드웨어 값이 바뀐 뒤 각 결과가 무엇을 보는지 비교하라.

<details>
<summary>답 보기</summary>

`auto traced_bad(F f) { return f(); }` 는 `uint32_t` 를 반환 → 호출 시점 값에 고정. `decltype(auto) traced_good(F f) { return f(); }` 는 `const volatile uint32_t&` 를 그대로 반환 → 별칭이라 나중 변화가 보인다. 주의: `decltype(auto)` 에서 `return (local);` 처럼 괄호를 치면 지역 변수 참조를 반환하는 댕글링이 생긴다.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 16/16 passed
```

## 10. 핵심 정리

- 접근자 반환 타입이 권한이다: RO → `volatile const uint32_t&`, RW → `volatile uint32_t&`, WO → `operator=` 만 있는 프록시.
- 참조의 cv 는 **더하기만** 자동, 빼기는 에러 (`const_cast` 는 탈출구일 뿐). 폴링 매개변수는 `const volatile&`.
- "컴파일되면 안 됨" 증명 = concept 안의 `requires` + `static_assert(!...)`. 템플릿 밖 `requires` 는 하드 에러.
- 멤버 함수 `const` 로 "const 핸들 = 읽기 전용 뷰" 정책을 만든다 (C++ const 는 얕다).
- `auto` 는 참조·volatile 을 버린다 → 폴링 hoist. 별칭은 `const volatile auto&` / `decltype(auto)`.
- `-O2` 실측: volatile 은 `ldr` 이 루프 안, non-volatile 은 hoist, 빈 무한 루프는 C++ forward-progress 로 **삭제**(`ret`).
- C++20: `REG++`, 대입 결과 사용, volatile 매개변수 → `-Wdeprecated-volatile`. `REG |= bit` 는 P2327 로 OK (clang 21 경고 없음). 카운터는 `REG = REG + 1;`.

## 참고자료

- [cppreference — cv (const and volatile) type qualifiers](https://en.cppreference.com/w/cpp/language/cv)
- [cppreference — Requires expression](https://en.cppreference.com/w/cpp/language/requires) · [placeholder type specifiers (auto, decltype(auto))](https://en.cppreference.com/w/cpp/language/auto)
- [P1152R4 — Deprecating volatile (JF Bastien)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1152r4.html)
- [P2327R1 — De-deprecating volatile compound operations](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p2327r1.pdf)
- [P2809R3 — Trivial infinite loops are not Undefined Behavior (C++26)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p2809r3.html)
- C++ 표준 [dcl.type.cv], [dcl.init.ref], [expr.ass], [expr.prim.req], [intro.abstract], [intro.progress], [depr.volatile.type] — [eel.is/c++draft](https://eel.is/c++draft/)
- [Clang diagnostics — -Wdeprecated-volatile](https://clang.llvm.org/docs/DiagnosticsReference.html#wdeprecated-volatile)
- [ARM CMSIS-Core — `__I/__O/__IO`, `__IM/__OM/__IOM` 정의](https://arm-software.github.io/CMSIS_6/latest/Core/index.html)
- STM32G0 Reference Manual RM0444 — USART_ISR(RO) / USART_ICR(WO) / RDR / TDR 레지스터 설명
- 관련 레슨: C-B09(volatile 의미와 한계), C-M01(MMIO 레지스터 구조체), C-A02(배리어), CPP-B04, CPP-B12, CPP-M01
