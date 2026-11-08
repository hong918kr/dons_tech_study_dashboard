---
id: CPP-B08
time: 50분
updated: 2026-11-07
---
# CPP-B08 클래스 레이아웃과 sizeof: 패딩·빈 클래스·EBO

## 학습 목표

- C++ 클래스의 `sizeof` / `alignof` / 멤버 오프셋을 **컴파일하기 전에** 예측한다: 빈 클래스(1바이트), `virtual` 이 넣는 vptr, 빈 베이스 최적화(EBO), `[[no_unique_address]]`.
- `std::is_standard_layout` / `std::is_trivially_copyable` / `std::is_trivial` 이 각각 **무엇을 허락하는지** 구분한다 (offsetof, memcpy·DMA, C 와 같은 레이아웃).
- 레지스터 블록 클래스를 **standard-layout 으로 유지**해 MMIO 에 겹쳐 쓰고, `static_assert` 로 레이아웃을 빌드 시간에 고정한다.
- 같은 코드가 arm64(포인터 8바이트)와 Cortex-M(포인터 4바이트)에서 어떻게 달라지는지 `-fdump-record-layouts` 로 직접 확인한다.

## 1. 문제

펌웨어에서 클래스 레이아웃은 "구현 세부사항"이 아니라 **하드웨어와의 계약**이다 — 레지스터 블록을 MMIO 에 겹쳐 쓰고, DMA 디스크립터를 memcpy 로 넘기고, C HAL 과 구조체를 주고받는다. 면접에서도 "`sizeof` 가 얼마냐 — 왜?" 는 C++ 펌웨어 직군의 단골 꼬리질문이다.

구조체 패딩·정렬의 기본(멤버는 자기 정렬 배수 오프셋에, 전체 크기는 최대 정렬의 배수)은 C 와 똑같다 (C-B12 참고). 이 레슨은 **C++ 가 더하는 것**만 다룬다.

| C++ 가 더하는 것 | 레이아웃에 미치는 영향 |
|---|---|
| 빈 클래스 `struct Empty {};` | `sizeof == 1` (C 에선 GNU 확장으로 0) |
| `virtual` 함수 | 객체 맨 앞에 **vptr**(포인터 1개) 추가 → 모든 멤버가 밀림 |
| 빈 베이스 클래스 | **EBO**: 0바이트로 접힘 (조건 있음) |
| `[[no_unique_address]]` (C++20) | 빈 *멤버* 도 0바이트로 |
| 상속 · 접근 지정자 | standard-layout 여부를 바꿈 → `offsetof` 와 C 호환성이 갈림 |

```svg
<svg viewBox="0 0 720 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">arm64 에서의 실제 배치 (칸 1개 = 1바이트)</text>
  <g fill="currentColor" font-size="12" text-anchor="middle" opacity=".7">
    <text x="213" y="42">0</text><text x="317" y="42">4</text><text x="421" y="42">8</text><text x="525" y="42">12</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="10" y="67">PadBad</text><text x="10" y="102">PadGood</text><text x="10" y="137">WithVirt</text>
    <text x="10" y="172">HasEmptyMember</text><text x="10" y="207">EboDerived</text>
  </g>
  <g stroke="currentColor">
    <rect x="200" y="50" width="26" height="24" class="f-acc"/><rect x="226" y="50" width="78" height="24" class="f-mut"/>
    <rect x="304" y="50" width="104" height="24" class="f-acc"/><rect x="408" y="50" width="26" height="24" class="f-acc"/>
    <rect x="434" y="50" width="78" height="24" class="f-mut"/>
    <rect x="200" y="85" width="104" height="24" class="f-acc"/><rect x="304" y="85" width="26" height="24" class="f-acc"/>
    <rect x="330" y="85" width="26" height="24" class="f-acc"/><rect x="356" y="85" width="52" height="24" class="f-mut"/>
    <rect x="200" y="120" width="208" height="24" class="f-warn"/><rect x="408" y="120" width="104" height="24" class="f-acc"/>
    <rect x="512" y="120" width="104" height="24" class="f-mut"/>
    <rect x="200" y="155" width="26" height="24" class="f-acc"/><rect x="226" y="155" width="78" height="24" class="f-mut"/>
    <rect x="304" y="155" width="104" height="24" class="f-acc"/>
    <rect x="200" y="190" width="104" height="24" class="f-acc"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">
    <text x="213" y="67">a</text><text x="265" y="67">pad</text><text x="356" y="67">b</text><text x="421" y="67">c</text><text x="473" y="67">pad</text>
    <text x="252" y="102">b</text><text x="317" y="102">a</text><text x="343" y="102">c</text><text x="382" y="102">pad</text>
    <text x="304" y="137">vptr</text><text x="460" y="137">x</text><text x="564" y="137">pad</text>
    <text x="213" y="172">e</text><text x="265" y="172">pad</text><text x="356" y="172">x</text>
    <text x="252" y="207">x</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="625" y="67">= 12</text><text x="625" y="102">= 8</text><text x="625" y="137">= 16</text>
    <text x="625" y="172">= 8</text><text x="625" y="207">= 4</text><text x="315" y="207">← Empty 베이스는 @0 에 0바이트</text>
  </g>
  <text x="10" y="240" fill="currentColor">빈 멤버 e 는 1바이트 + 패딩 3 을 먹지만, 빈 베이스는 0바이트(EBO). vptr 는 정렬까지 8로 끌어올린다.</text>
  <text x="10" y="260" fill="currentColor">Cortex-M(ILP32)에선 vptr 가 4바이트 → WithVirt 는 8. 나머지 줄은 포인터가 없어서 같다.</text>
</svg>
```

### Part A — 예측표를 채우고 static_assert 로 고정

`starter.cpp` 의 `namespace pred` 에 아래 타입들의 값을 **컴파일러에 묻기 전에** 채운다 (arm64 기준; 포인터가 끼는 값은 `sizeof(void*)` 로 쓰면 Cortex-M 에서도 맞는다). 맞았으면 `static_assert` 로 고정한다 — 앞으로 누가 멤버를 바꾸면 **빌드가 깨진다**.

| 타입 | 정의 | 예측할 값 |
|---|---|---|
| `Empty` | `struct Empty {};` | sizeof |
| `PadBad` / `PadGood` | `u8,u32,u8` / `u32,u8,u8` | sizeof (C 복습) |
| `WithVirt` | `virtual void on_irq(); u32 x;` | sizeof, alignof, x 의 오프셋 |
| `HasEmptyMember` | `Empty e; u32 x;` | sizeof, x 의 오프셋 |
| `EboDerived` | `: Empty { u32 x; }` | sizeof |
| `EboClash` | `: Empty { Empty e; u32 x; }` | sizeof |
| `NuaMember` | `[[no_unique_address]] Empty e; u32 x;` | sizeof |

### Part B — 타입 특성 예측 + 꼬리 패딩 + 0바이트 정책

1. 6개 타입에 대해 `{standard_layout, trivially_copyable, trivial}` 세 값을 예측한다: `WithVirt`, `UserCtor`(생성자 직접 작성), `UserCopy`(복사 생성자 직접 작성), `MixedAccess`(public/private 데이터 섞임), `Derived4`(베이스·파생 양쪽에 데이터), `EboClash`.
2. `AfterPod : PodBase { u8 c; }` 와 `AfterNonPod : NonPodBase { u8 c; }` — 베이스 정의는 생성자 하나 차이뿐인데 sizeof 가 다르다. 둘 다 예측.
3. `EventCounter<Lock>` 은 락 정책(policy)을 멤버로 가진다. ISR 공유가 없는 빌드용 `NoLock` 은 빈 타입인데 지금은 4바이트(1 + 패딩 3)를 낭비한다. **C++20 한 단어**로 `sizeof(EventCounter<NoLock>) == sizeof(uint32_t)` 를 만든다.

### Part C — MMIO 레지스터 블록 클래스와 DMA 디스크립터

가상의 UART 데이터시트 (STM32 USART 스타일):

| 오프셋 | 레지스터 | 접근 | 비트 |
|---|---|---|---|
| 0x00 | CR | RW | bit0 UE(enable), bit3 TE(tx enable) |
| 0x04 | SR | RO | bit7 TXE(송신 레지스터 비었음) |
| 0x08–0x0F | (reserved) | — | — |
| 0x10 | BRR | RW | baud 분주 = round(pclk / baud) |
| 0x14 | DR | RW | 데이터 하위 8비트 |
| 0x18 / 0x1A | FIFO_LVL / FIFO_THR | RO / RW | 각 16비트. 블록 크기 0x1C |

starter 의 `class UartRegs` 에는 **레이아웃을 깨는 문제가 2개** 있다. 찾아서 고치고, `enable_tx` / `set_baud` / `tx_ready` / `write` / `fifo_level` 을 채우고, `static_assert` 로 standard-layout·오프셋·크기를 고정한다. 테스트는 "하드웨어 쪽" 에서 **오프셋으로만** 바이트를 읽고 쓴다 (`hw_read32(0x10)` 등) — 레이아웃이 틀리면 동작도 틀린다.

마지막으로 DMA 디스크립터를 디스크립터 RAM 에 memcpy 하는 `post_desc` 와, 그래도 되는 타입만 받는 concept `DmaSafe` 를 작성한다 (`BadDesc` 처럼 vptr 가 있는 타입, `UserCopy` 처럼 복사에 사용자 코드가 필요한 타입은 거부).

## 2. Problem (English)

> **Predict and lock down C++ object layout for firmware.**
>
> Target: arm64 (LP64), Itanium-family C++ ABI, `-std=c++20 -fno-exceptions -fno-rtti`.
>
> 1. Without compiling, give `sizeof`/`alignof`/member offsets for: an empty class; a class with one virtual function and a `uint32_t`; a class with an empty *member* vs. an empty *base*; a class whose empty base has the same type as its first member; and a class with a `[[no_unique_address]]` empty member. Lock each answer with `static_assert`. State which values change on a 32-bit Cortex-M.
> 2. For six given classes, predict `std::is_standard_layout`, `std::is_trivially_copyable`, `std::is_trivial`. Explain why `struct D : B { uint8_t c; }` can be 8 bytes when `B` has a user-provided constructor, but 12 bytes when `B` is an aggregate.
> 3. Make a stateless lock policy cost zero bytes inside `template<class Lock> class EventCounter`.
> 4. Fix a `UartRegs` class so it can be overlaid on MMIO at the datasheet offsets (`BRR` at `0x10`, size `0x1C`), implement its accessors, and add compile-time layout checks. Write `concept DmaSafe` and `post_desc(slot, desc)` that copies a descriptor into descriptor RAM with `memcpy`, rejecting types for which that is invalid.
>
> Example: `set_baud(16'000'000, 115'200)` must store `139` at offset `0x10`.

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 문제 회피
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
# 처음엔 "9/41 passed" — 예측값과 TODO 를 채워 41/41 을 만든다
```

## 4. 힌트

<details>
<summary>힌트 1 — 빈 클래스는 왜 0이 아닌가</summary>

C++ 에서 서로 다른 두 객체는 (한쪽이 다른 쪽 안에 있거나, 크기 0 인 서로 다른 타입의 부분객체가 아닌 한) **주소가 달라야** 한다. `Empty a[2];` 에서 `&a[0] != &a[1]` 이 되려면 크기가 최소 1 이어야 한다.

</details>

<details>
<summary>힌트 2 — virtual 하나가 붙이는 것</summary>

다형 클래스 객체에는 "내 vtable 이 어디 있나"를 가리키는 숨은 포인터가 들어간다. Itanium 계열 ABI(clang/gcc, ARM EABI)는 이것을 **오프셋 0** 에 둔다. 포인터 크기·정렬이 그대로 클래스에 전염된다.

</details>

<details>
<summary>힌트 3 — EBO 가 실패하는 경우</summary>

빈 베이스는 0바이트로 접힐 수 있지만, **같은 타입의 두 부분객체**는 주소가 달라야 한다. 베이스가 `Empty` 이고 첫 멤버도 `Empty` 면 둘 다 오프셋 0 에 둘 수 없다.

</details>

<details>
<summary>힌트 4 — 세 가지 trait 의 질문</summary>

standard-layout: "C 구조체처럼 배치가 예측 가능한가?" (virtual 없음, 데이터 멤버 접근 지정 동일, 데이터 멤버가 한 클래스에만…). trivially copyable: "바이트 복사가 곧 복사인가?" (복사/이동/소멸자가 사용자 코드 없이 trivial). trivial: trivially copyable + 기본 생성자도 trivial.

</details>

<details>
<summary>힌트 5 — 레지스터 블록에서 금지되는 것</summary>

`virtual` (vptr 가 CR 자리를 차지), 예약 영역을 빼먹기, 데이터 멤버를 public/private 로 나누기. 멤버 *함수* (비가상)는 레이아웃에 0바이트라 마음껏 넣어도 된다.

</details>

## 5. 풀이

### Part A — 예측표 정답

| 타입 | arm64 sizeof / align | Cortex-M (예상·덤프 확인) | 이유 |
|---|---|---|---|
| `Empty` | 1 / 1 | 1 / 1 | 서로 다른 객체는 다른 주소 ([intro.object]) |
| `PadBad` | 12 / 4 | 12 / 4 | C 와 동일 (C-B12) |
| `PadGood` | 8 / 4 | 8 / 4 | 큰 것부터 |
| `WithVirt` | **16 / 8**, x @ 8 | **8 / 4**, x @ 4 | vptr(포인터) @0 → `2 * sizeof(void*)` |
| `HasEmptyMember` | 8 / 4, x @ 4 | 같음 | 빈 *멤버* 는 1바이트 + 패딩 3 |
| `EboDerived` | **4** / 4 | 같음 | EBO: 빈 베이스는 x 와 같은 주소 @0 |
| `EboClash` | **8** / 4 | 같음 | 베이스 `Empty` @0, 멤버 `Empty` @1, x @4 |
| `NuaMember` | **4** / 4 | 같음 | `[[no_unique_address]]` → 멤버도 0바이트 |

Cortex-M 열은 실기기가 아니라 `clang --target=thumbv7em-none-eabi` 의 **프런트엔드 레이아웃 덤프** 로 확인한 값이다 (arm-none-eabi-g++ 도 같은 ARM C++ ABI 규칙을 따르므로 같은 값을 기대). 덤프 명령과 실제 출력:

```bash
c++ -std=c++20 -fsyntax-only -Xclang -fdump-record-layouts demo_layout.cpp                                # arm64
c++ -std=c++20 --target=thumbv7em-none-eabi -fsyntax-only -Xclang -fdump-record-layouts demo_layout.cpp   # Cortex-M4
```

```text
== arm64                                  == thumbv7em-none-eabi
   0 | struct WithVirt                        0 | struct WithVirt
   0 |   (WithVirt vtable pointer)            0 |   (WithVirt vtable pointer)
   8 |   u32 x                                4 |   u32 x
     | [sizeof=16, dsize=12, align=8,           | [sizeof=8, dsize=8, align=4,

   0 | struct EboClash                        (양쪽 동일)
   0 |   struct Empty (base) (empty)
   1 |   struct Empty e (empty)
   4 |   u32 x
     | [sizeof=8, dsize=8, align=4,

   0 | struct NuaMember                       (양쪽 동일)
   0 |   struct Empty e (empty)
   0 |   u32 x                                ← e 와 x 가 같은 오프셋 0
     | [sizeof=4, dsize=4, align=4,
```

규칙을 정리하면:

1. **빈 클래스 = 1바이트.** 완전한 객체(most-derived object)와 일반 멤버는 크기가 0 이 될 수 없다. 단 **잠재적으로 겹칠 수 있는 부분객체(potentially-overlapping subobject)** — 베이스 클래스 부분객체와 `[[no_unique_address]]` 멤버 — 는 크기 0 이 허용된다 ([intro.object]).
2. **EBO 는 "다른 타입끼리만" 겹친다.** 같은 타입의 두 객체는 주소가 달라야 하므로 `EboClash` 는 멤버 `e` 를 1 로 밀고, 그 뒤 `x` 는 정렬 때문에 4 로 간다.
3. **vptr 는 오프셋 0.** 표준은 vptr 의 존재조차 말하지 않는다(구현 방식) — 위치는 Itanium C++ ABI §2.4 가 정한다. 포인터 크기·정렬이 그대로 따라오므로 **타깃마다 값이 다른 유일한 줄**이다. 그래서 solution 은 `2 * sizeof(void*)` 로 썼다.
4. `WithVirt` 의 x 오프셋은 `offsetof` 로 잴 수 **없다** — standard-layout 이 아니기 때문 (Part C 에서 실제 에러를 본다). 하네스의 `addr_off(obj, obj.x)` 는 주소 차이로 재는 측정기다.

static_assert 고정은 "테스트를 빌드로 옮기는 것"이다. 실행할 필요도 없고, 코드 크기도 0 이다.

```cpp
static_assert(sizeof(WithVirt) == pred::WithVirt_size && alignof(WithVirt) == pred::WithVirt_align);
static_assert(offsetof(HasEmptyMember, x) == pred::HasEmptyMember_x_off);   // SL 이라 OK
```

### Part B — 세 가지 trait 가 묻는 것

| trait | 질문 | 깨뜨리는 것 (대표) | 허락하는 것 |
|---|---|---|---|
| `is_standard_layout` ([class.prop]/3) | C 구조체처럼 배치가 예측 가능한가 | virtual 함수·가상 베이스, 데이터 멤버의 접근 지정이 섞임, 데이터 멤버가 베이스·파생 **양쪽**에 있음, 첫 멤버 타입이 베이스와 같음, 참조 멤버 | `offsetof`, 첫 멤버와 주소 공유(pointer-interconvertible), C 와 같은 배치 |
| `is_trivially_copyable` ([class.prop]/1) | 바이트 복사가 곧 복사인가 | 사용자 제공 복사/이동 생성자·대입·소멸자, virtual | `memcpy` / `std::bit_cast` 로 값 옮기기 ([basic.types.general], C++20 번호로는 [basic.types]/2–3) |
| `is_trivial` ([class.prop]/2) | 위 + 기본 생성자도 아무 일 안 하나 | 사용자 생성자, **기본 멤버 초기화자** `uint32_t x = 0;` | 초기화 없이 메모리에 "그냥 존재" (C++26 에서 deprecated 예정, P3247) |

예측 정답과 이유:

| 타입 | SL | TC | trivial | 결정적인 이유 |
|---|---|---|---|---|
| `WithVirt` | ✗ | ✗ | ✗ | virtual → 셋 다 탈락 |
| `UserCtor` | ✓ | ✓ | ✗ | 생성자는 *복사*와 무관 → TC 는 유지, trivial 만 탈락 |
| `UserCopy` | ✓ | ✗ | ✗ | 복사 생성자를 직접 작성 → 바이트 복사 ≠ 복사 |
| `MixedAccess` | ✗ | ✓ | ✓ | public `a` + private `b` (멤버 함수는 상관없음) |
| `Derived4` | ✗ | ✓ | ✓ | `a` 는 Base4, `b` 는 Derived4 에 선언 |
| `EboClash` | ✗ | ✓ | ✓ | 첫 멤버 타입(Empty) == 베이스 타입 |

세 trait 는 **서로 독립적인 축**이다. "trivial 하면 standard-layout" 도, 그 반대도 아니다. 둘 다 만족하던 것이 C++03 의 POD 이고, `std::is_pod` 는 C++20 에서 deprecated 됐다 — 필요한 성질을 각각 따로 물으라는 뜻이다.

```svg
<svg viewBox="0 0 720 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <rect x="20" y="40" width="420" height="185" fill="none" stroke="currentColor" stroke-width="2"/>
  <rect x="40" y="70" width="260" height="140" fill="none" stroke="currentColor"/>
  <rect x="200" y="55" width="500" height="185" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="6 4"/>
  <text x="28" y="34" fill="currentColor" font-weight="bold">trivially copyable (바이트 복사 OK)</text>
  <text x="48" y="90" fill="currentColor">trivial</text>
  <text x="210" y="105" fill="currentColor" font-weight="bold">= POD</text>
  <text x="450" y="78" fill="currentColor" font-weight="bold">standard-layout (점선)</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="50" y="125">MixedAccess</text><text x="50" y="145">Derived4</text><text x="50" y="165">EboClash</text>
    <text x="210" y="130">PadBad</text><text x="210" y="150">DmaDesc</text><text x="210" y="170">Empty</text>
    <text x="310" y="130">UserCtor</text>
    <text x="460" y="130">UserCopy</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="310" y="150">TC, 생성자만 직접</text>
    <text x="460" y="150">SL 이지만 복사에 사용자 코드</text>
  </g>
  <text x="10" y="262" fill="currentColor">어디에도 못 들어감: WithVirt, BadDesc (vptr) — virtual 하나로 세 성질을 모두 잃는다.</text>
  <text x="10" y="285" fill="currentColor">MMIO 오버레이 → SL 필요 · memcpy/DMA/bit_cast → TC 필요 (+ 오프셋을 상대와 맞추려면 SL 도)</text>
</svg>
```

**꼬리 패딩 재사용 (tail padding reuse).** `PodBase` 와 `NonPodBase` 는 둘 다 `{u32 a; u8 b;}` 로 sizeof 8, 그중 실제 데이터는 5바이트(덤프의 `dsize=5`)다. 나머지 3바이트는 꼬리 패딩이다.

```text
   0 | struct AfterNonPod                     [arm64, thumbv7em 동일]
   0 |   struct NonPodBase (base)
   0 |     u32 a
   4 |     u8 b
   5 |   u8 c                  ← 베이스의 꼬리 패딩 안으로 들어감
     | [sizeof=8, dsize=6, align=4,
```

Itanium ABI 는 "레이아웃 목적상 POD(C++03 POD)" 가 **아닌** 베이스의 꼬리 패딩에 파생 클래스 멤버를 채워 넣는다. `NonPodBase` 는 생성자 하나 때문에 non-POD → `AfterNonPod` 는 8. `PodBase` 는 POD → 패딩을 건드리지 않아 `AfterPod` 는 12. 표준이 아니라 **ABI 가 정하는 값**이다.

이게 왜 위험한가: `NonPodBase` 는 trivially copyable 이지만, `AfterNonPod` 안의 베이스 부분객체에 `memcpy(base_ptr, src, sizeof(NonPodBase))` 하면 8바이트를 쓰면서 오프셋 5 의 `c` 를 덮는다. 그래서 표준은 memcpy 복사를 허락하는 조건에 "둘 다 잠재적으로 겹치는 부분객체가 아닐 것" 을 붙여 두었다 ([basic.types.general]/3). 연습 3 에서 확인한다.

**0바이트 정책 (`EventCounter`).** 정책 클래스를 멤버로 두면 빈 타입이라도 1바이트 + 정렬 패딩을 먹는다. 방법은 둘:

| 방법 | 코드 | 비고 |
|---|---|---|
| C++20 | `[[no_unique_address]] Lock lock_;` | 멤버 그대로, 읽기 쉬움. **MSVC 는 무시** → `[[msvc::no_unique_address]]` |
| C++11 EBO | `class EventCounter : private Lock` | 상속이라 이름 충돌·`final` 클래스 불가 등 불편. `std::unique_ptr` 의 deleter 가 옛날 방식 |

`EventCounter<NoLock>` 은 4바이트(= C 에서 `uint32_t count` 하나 쓴 것과 동일), `EventCounter<IrqLock>`(PRIMASK 저장값을 가진 상태 있는 락, CPP-B06 참고)은 8바이트. 정책 선택이 **런타임 비용도 메모리 비용도 0** 이 되는 것이 템플릿 정책 설계의 핵심이다.

### Part C — 레지스터 블록: 두 개의 버그

```svg
<svg viewBox="0 0 720 320" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">같은 UartRegs, 세 가지 배치 (한 줄 = 4바이트)</text>
  <g fill="currentColor" font-weight="bold" text-anchor="middle">
    <text x="160" y="42">데이터시트 = 고친 버전</text><text x="350" y="42">starter (virtual) arm64</text><text x="540" y="42">starter (virtual) Cortex-M</text>
  </g>
  <g fill="currentColor" font-family="monospace" opacity=".8">
    <text x="20" y="67">0x00</text><text x="20" y="91">0x04</text><text x="20" y="115">0x08</text><text x="20" y="139">0x0C</text>
    <text x="20" y="163">0x10</text><text x="20" y="187">0x14</text><text x="20" y="211">0x18</text><text x="20" y="235">0x1C</text>
  </g>
  <g stroke="currentColor">
    <rect x="90" y="50" width="140" height="24" class="f-ok"/><rect x="90" y="74" width="140" height="24" class="f-ok"/>
    <rect x="90" y="98" width="140" height="48" fill="none"/>
    <rect x="90" y="146" width="140" height="24" class="f-ok"/><rect x="90" y="170" width="140" height="24" class="f-ok"/>
    <rect x="90" y="194" width="140" height="24" class="f-ok"/>
    <rect x="280" y="50" width="140" height="48" class="f-mut"/>
    <rect x="280" y="98" width="140" height="24" class="f-warn"/><rect x="280" y="122" width="140" height="24" class="f-warn"/>
    <rect x="280" y="146" width="140" height="24" class="f-ok"/><rect x="280" y="170" width="140" height="24" class="f-ok"/>
    <rect x="280" y="194" width="140" height="24" class="f-ok"/><rect x="280" y="218" width="140" height="24" fill="none" stroke-dasharray="4 3"/>
    <rect x="470" y="50" width="140" height="24" class="f-mut"/>
    <rect x="470" y="74" width="140" height="24" class="f-warn"/><rect x="470" y="98" width="140" height="24" class="f-warn"/>
    <rect x="470" y="122" width="140" height="24" class="f-warn"/><rect x="470" y="146" width="140" height="24" class="f-warn"/>
    <rect x="470" y="170" width="140" height="24" class="f-warn"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="160" y="67">CR</text><text x="160" y="91">SR</text><text x="160" y="127">reserved0_[2]</text>
    <text x="160" y="163">BRR</text><text x="160" y="187">DR</text><text x="160" y="211">LVL | THR</text>
    <text x="350" y="79">vptr (8B)</text><text x="350" y="115">CR  ✗</text><text x="350" y="139">SR  ✗</text>
    <text x="350" y="163">BRR ✓ (우연)</text><text x="350" y="187">DR  ✓</text><text x="350" y="211">LVL | THR ✓</text>
    <text x="350" y="235">pad → 32B</text>
    <text x="540" y="67">vptr (4B)</text><text x="540" y="91">CR  ✗</text><text x="540" y="115">SR  ✗</text>
    <text x="540" y="139">BRR ✗</text><text x="540" y="163">DR  ✗</text><text x="540" y="187">LVL | THR ✗</text>
  </g>
  <text x="10" y="270" fill="currentColor" font-size="13">arm64: vptr(8B) 가 우연히 예약 영역 크기와 같아 BRR 이하는 맞아 보인다.</text>
  <text x="10" y="292" fill="currentColor" font-size="13">Cortex-M: vptr(4B) 만큼 전부 밀린다. 오프셋 테스트만으론 부족 → SL static_assert 필수.</text>
  <text x="10" y="312" fill="currentColor" font-size="13">(starter 실행: BRR/DR/FIFO 오프셋 CHECK 는 PASS, CR/SR·SL·sizeof 는 FAIL)</text>
</svg>
```

**버그 1 — `virtual void dump()`.** 디버그 편의로 넣은 가상 함수 하나가 오프셋 0 에 vptr 를 끼워 넣어 모든 레지스터를 민다. 게다가 실제 MMIO 에는 vptr 가 없으므로 `dump()` 를 가상 호출하면 **레지스터 값(CR)을 vtable 주소로 해석해 점프**한다. 고침: `virtual` 제거 (비가상 멤버 함수는 0바이트).

**버그 2 — 예약 영역 누락.** 0x08–0x0F 를 `uint32_t reserved0_[2];` 로 채운다. CMSIS 헤더의 `RESERVED0[2]` 와 같은 관례다. 주의: 이 멤버를 `private:` 로 숨기면 데이터 멤버의 접근 지정이 섞여 standard-layout 이 깨진다 — **모두 public 이거나 모두 private** 이어야 한다. 그래서 이름에 `_` 를 붙여 "만지지 말 것" 을 표시만 한다.

왜 standard-layout 이 필요조건인가:

| 보장 | 근거 | 레지스터 블록에서의 의미 |
|---|---|---|
| `offsetof` 사용 가능 | [support.types.layout]/1: SL 이 아니면 *conditionally-supported* (clang 은 `-Winvalid-offsetof`) | 데이터시트 오프셋을 static_assert 로 검증 |
| 객체 주소 == 첫 멤버 주소 | [basic.compound]/4 pointer-interconvertible | `reinterpret_cast<UartRegs*>(0x4001'1000)` 의 `CR` 이 정확히 0x4001'1000 |
| C 구조체와 같은 배치 | layout-compatible ([class.mem]) | C HAL 헤더의 `USART_TypeDef` 와 같은 메모리를 공유 |
| 선언 순서 = 주소 순서 | [class.mem]: C++20 까지는 "접근 지정이 같은 멤버끼리만" 보장, C++23 (P1847) 부터 전부 보장 | 접근 지정을 섞지 않으면 C++20 에서도 안전 |

`demo_offsetof.cpp` 로 실제 에러를 확인했다 (`-Werror` 빌드):

```text
$ c++ -std=c++20 -Wall -Wextra -Werror -fsyntax-only -DSHOW_ERROR demo_offsetof.cpp
demo_offsetof.cpp:16:28: error: 'offsetof' on non-standard-layout type 'RegsVirt' [-Werror,-Winvalid-offsetof]
demo_offsetof.cpp:17:28: error: 'offsetof' on non-standard-layout type 'RegsMixed' [-Werror,-Winvalid-offsetof]
demo_offsetof.cpp:18:15: error: static assertion failed due to requirement 'std::is_standard_layout_v<RegsVirt>': MMIO overlay must be standard-layout
demo_offsetof.cpp:18:15: note: 'RegsVirt' is not standard-layout
demo_offsetof.cpp:18:15: note: because it has a virtual function 'dump'
3 errors generated.
```

`RegsMixed` 는 실제 배치가 `{CR @0, SR @4}` 로 똑같은데도 거부된다 — "지금 이 컴파일러에서 우연히 같은 것" 과 "표준이 보장하는 것" 의 차이다.

**메서드.** 비가상 멤버 함수는 객체에 아무것도 추가하지 않는다 (`this` 를 첫 인자로 받는 평범한 함수). C 의 `uart_set_baud(USART_TypeDef*, ...)` 와 같은 기계어가 나온다.

```cpp
void enable_tx() { CR |= CR_UE | CR_TE; }                          // RMW, 다른 비트 보존
void set_baud(uint32_t pclk, uint32_t baud) { BRR = (pclk + baud / 2) / baud; }   // 반올림
bool tx_ready() const { return (SR & SR_TXE) != 0; }
```

`16'000'000 / 115'200 = 138.89` → 반올림 `(16'000'000 + 57'600) / 115'200 = 139`. 버림(138)이면 baud 오차가 +0.64% 로 커진다.

**레이아웃 고정.**

```cpp
static_assert(std::is_standard_layout_v<UartRegs>, "MMIO overlay must be standard-layout");
static_assert(offsetof(UartRegs, BRR) == 0x10 && offsetof(UartRegs, FIFO_THR) == 0x1A);
static_assert(sizeof(UartRegs) == 0x1C && alignof(UartRegs) == 4);
```

이 세 줄은 포인터가 없어서 arm64·Cortex-M 모두에서 성립한다 (demo_layout 덤프에서 두 타깃 모두 `sizeof=28, align=4` 확인).

**호스트 mock 에 대한 메모.** 실제 칩은 `reinterpret_cast<UartRegs*>(0x4001'1000)` — C++ 추상 기계 밖의 주소에 volatile 접근을 그대로 내보내는 **구현(컴파일러) 관행**에 기댄다. 호스트 mock 은 `alignas(8) unsigned char mmio[0x40]` 위에 겹친다. C++20 에서 `unsigned char` 배열은 **implicit-lifetime 타입** 객체를 암묵적으로 만들 수 있고 ([intro.object], P0593), 고친 `UartRegs` 는 trivial 기본 생성자 + trivial 소멸자라 implicit-lifetime 이다 (`-std=c++23` 의 `std::is_implicit_lifetime_v<UartRegs>` 로 1 확인). 그래서 `std::launder(reinterpret_cast<UartRegs*>(mmio))` 로 접근한다 (C++23 이면 `std::start_lifetime_as<UartRegs>`). starter 의 virtual 버전은 이 조건을 만족하지 못한다 — 애초에 겹쳐 쓰면 안 되는 타입이다. "하드웨어" 쪽은 `volatile unsigned char*` 로 바이트만 만지므로 aliasing 규칙에도 걸리지 않는다. solution 은 `-fsanitize=address,undefined` 로도 깨끗하다 (41/41).

**DMA 디스크립터와 `DmaSafe`.**

```cpp
template <class T>
concept DmaSafe = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>;
```

- trivially copyable: memcpy 한 바이트가 "그 값" 이어야 한다. vptr 가 있는 `BadDesc` 나 복사 생성자가 있는 `UserCopy` 는 탈락.
- standard-layout: DMA 엔진은 C++ 를 모른다 — `src @0, dst @4, len @8, ctrl @12` 라는 오프셋 계약이 있어야 한다.
- 레지스터 블록과 반대 방향인 점에 주의: `UartRegs` 는 복사를 `= delete` 했다 (레지스터를 "복사"하는 건 의미가 없다). 오버레이에 필요한 건 SL 이지 TC 가 아니다.

C 와 비교하면:

| C 에서 하던 것 | C++ 에서 | 런타임 비용 |
|---|---|---|
| `typedef struct { volatile uint32_t CR; ... } USART_TypeDef;` | 같은 구조 + 비가상 메서드 | 0 (메서드 = this 를 받는 함수) |
| 오프셋 검증 없음 또는 `_Static_assert(offsetof(...))` | `static_assert` + `is_standard_layout` 로 "C++ 기능을 잘못 섞으면" 바로 에러 | 0 (컴파일 시간) |
| `void*` 를 받는 `dma_post(const void*, size_t)` | `template <DmaSafe T>` 로 타입 검사 | 0 |
| 빈 구조체 불가 (GNU 확장 0바이트) | 빈 정책 클래스 + `[[no_unique_address]]` | 0 바이트 |

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution   # 41/41 passed
c++ -std=c++20 -g -O1 -fsanitize=address,undefined solution.cpp -o solution_asan && ./solution_asan          # 41/41, 경고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 레지스터 블록에 `virtual void dump()` | vptr 가 오프셋 0 을 차지 → 모든 레지스터가 4/8 밀림, SL 깨짐 | 비가상 함수 또는 자유 함수 `dump(const UartRegs&)` |
| 예약 레지스터를 `private:` 로 숨김 | 데이터 멤버 접근 지정이 섞여 SL 아님 → `offsetof` 경고/에러 | 모두 public + `reserved0_` 이름 관례 |
| "`sizeof(Empty)` 는 0" | 완전한 객체는 최소 1바이트. 0 은 베이스/`[[no_unique_address]]` 일 때만 | 정책은 베이스 또는 `[[no_unique_address]]` 멤버로 |
| `[[no_unique_address]]` 붙였으니 같은 타입 빈 멤버 2개도 0 | 같은 타입 두 객체는 주소가 달라야 함 → `Pair<Empty,Empty>` 는 2 | 타입을 다르게 (태그 타입) |
| 다형 클래스 크기를 arm64 값으로 하드코딩 `static_assert(sizeof(V)==16)` | Cortex-M 에선 8 → 타깃 빌드가 깨짐 | `2 * sizeof(void*)` 처럼 포인터 기준으로 |
| `is_trivial` 로 "memcpy 해도 되나" 판단 | trivial 은 *기본 생성자*까지 요구 — `UserCtor` 는 memcpy 해도 되는데 거부됨 | `is_trivially_copyable` 을 쓴다 |
| `is_trivially_copyable` 이면 베이스 부분객체에 memcpy 해도 된다 | 꼬리 패딩에 파생 멤버가 살 수 있음 ([basic.types.general]/3) | 완전한 객체 단위로 복사 |
| 패딩 있는 구조체 바이트로 CRC 계산 | 패딩 바이트 값은 미정 → 같은 값인데 CRC 가 다름 | `has_unique_object_representations` 확인 + 패딩 자리를 명시 멤버로 (연습 4) |
| `UART->CR++` 같은 volatile 증감 | C++20 에서 deprecated (`-Wdeprecated-volatile`). 비트 복합대입 `\|=` 은 C++23 (P2327) 에서 되살아남 — clang 21 은 `-std=c++20` 에서도 `\|=` 경고 없음 (확인) | `CR = CR + 1` 처럼 읽기/쓰기를 드러내기 (CPP-B05 참고) |

## 8. 면접 Q&A

<details>
<summary>Q1. 빈 클래스의 sizeof 는? 왜 0 이 아닌가요? C 와는 어떻게 다른가요?</summary>

**1바이트.** 서로 다른 객체는 다른 주소를 가져야 하고(`Empty a[2]` 에서 `&a[0] != &a[1]`), 포인터 산술이 성립하려면 크기가 0 이면 안 된다. C 에서는 멤버 없는 구조체 자체가 표준 위반이고 GCC/clang 확장으로 **sizeof 0** 이다 (`cc` 로 확인: `sizeof(struct E) == 0`, `-pedantic` 에서 `-Wgnu-empty-struct`). 그래서 C/C++ 공용 헤더에 빈 구조체를 두면 양쪽 ABI 가 어긋난다. 꼬리질문 "그럼 0바이트로 만들 방법은?" → 베이스로 상속(EBO) 또는 `[[no_unique_address]]` 멤버.

</details>

<details>
<summary>Q2. 클래스에 virtual 함수를 하나 추가하면 크기가 어떻게 되나요? 함수를 10개 추가하면?</summary>

객체당 vptr 1개 (포인터 크기, 보통 오프셋 0 — Itanium/ARM C++ ABI). Cortex-M 에선 +4바이트, arm64 에선 +8 에 정렬도 8 로 올라가 패딩이 더 붙을 수 있다. 가상 함수를 10개로 늘려도 객체 크기는 그대로 — 늘어나는 건 **클래스당 1개** 인 vtable(플래시의 읽기 전용 데이터)의 슬롯 수다. 다중 상속이면 vptr 가 여러 개 생길 수 있다. 추가 비용: 간접 호출(인라인 불가), SL·TC·trivial 상실 (CPP-B13 참고: `-fno-rtti` 여도 vtable 은 남는다).

</details>

<details>
<summary>Q3. EBO 가 무엇이고 언제 실패하나요? C++20 에서는 무엇으로 대체하나요?</summary>

빈 베이스 클래스 부분객체가 0바이트를 차지하도록 파생 클래스의 첫 멤버와 같은 주소에 두는 최적화. 실패 조건: 같은 타입의 부분객체가 같은 주소에 올 때 (`struct D : Empty { Empty e; }` → 8). 표준은 일반적으로 EBO 를 강제하지 않지만, standard-layout 클래스는 "객체 주소 == 베이스 부분객체 주소 == 첫 멤버 주소" 를 요구하므로 사실상 강제된다. MSVC 는 빈 베이스가 여러 개면 `__declspec(empty_bases)` 가 있어야 다 접는다. C++20 `[[no_unique_address]]` 는 상속 없이 멤버로 같은 효과 — `std::unique_ptr<T, Deleter>` 같은 compressed pair 를 간단히 쓴다. 단 MSVC 는 이 속성을 무시한다(`[[msvc::no_unique_address]]`).

</details>

<details>
<summary>Q4. standard-layout, trivially copyable, trivial 차이를 펌웨어 예로 설명해 보세요.</summary>

- standard-layout: 배치가 C 처럼 예측 가능 → **MMIO 레지스터 블록 오버레이, `offsetof`, C HAL 과 구조체 공유**.
- trivially copyable: 바이트 복사 = 값 복사 → **memcpy 로 DMA 버퍼·플래시·패킷에 넣기, `std::bit_cast`**.
- trivial: TC + 기본 생성자도 trivial → **초기화 코드 없이** 존재 가능 (`.bss` 의 0 초기화만으로 충분, 큰 배열 생성자 루프 없음).
- 서로 독립이다: `UserCtor` 는 SL+TC 지만 trivial 아님, `MixedAccess` 는 trivial 이지만 SL 아님. 꼬리질문 "POD 는?" → SL ∩ trivial. `std::is_pod` 는 C++20 deprecated.

</details>

<details>
<summary>Q5. 레지스터 블록 클래스에서 private 멤버를 쓰면 안 되나요? 캡슐화하고 싶은데요.</summary>

데이터 멤버를 **전부** private 으로 하는 건 괜찮다 — standard-layout 조건은 "모든 비정적 데이터 멤버의 접근 지정이 같을 것" 이다 (연습 5). 문제는 섞을 때다. C++20 까지는 접근 지정이 다른 멤버 사이의 배치 순서가 *unspecified* 였다 (C++23 P1847 로 선언 순서 보장). 다만 전부 private 이면 클래스 밖에서 `offsetof(UartRegs, BRR)` 로 이름을 못 쓰므로, 검증 static_assert 를 클래스 안의 static 멤버 함수나 friend 에 둬야 한다. 실무에선 CMSIS 처럼 public 레지스터 구조체 + 그것을 감싸는 드라이버 클래스로 나누는 경우가 많다.

</details>

<details>
<summary>Q6. 클래스 D 가 B 를 상속하고 u8 하나를 추가했는데, B 에 생성자를 추가했더니 sizeof(D) 가 12 → 8 로 줄었습니다. 왜죠? 문제는 없나요?</summary>

Itanium C++ ABI 는 C++03 의미의 POD 가 아닌 베이스의 **꼬리 패딩(tail padding)** 을 파생 클래스 멤버로 재사용한다. B 가 생성자 때문에 non-POD 가 되자 D 의 `c` 가 B 의 패딩(오프셋 5)으로 들어갔다. 문제: `memcpy(static_cast<B*>(&d), &other_b, sizeof(B))` 가 `c` 를 덮어쓴다 — 그래서 표준은 잠재적으로 겹치는 부분객체에 대한 바이트 복사를 보장하지 않는다. 또 이것은 ABI 규칙이라 컴파일러/ABI 가 다르면 달라진다. `[[no_unique_address]]` 멤버의 꼬리 패딩도 같은 식으로 재사용될 수 있다.

</details>

<details>
<summary>Q7. 패딩이 있는 설정 구조체를 플래시에 저장하고 CRC 를 계산했더니 가끔 불일치합니다. 왜죠?</summary>

패딩 바이트의 값은 정해져 있지 않다. 지역 변수로 만들어 멤버만 대입하면 패딩에 스택 쓰레기가 남고, 같은 값이라도 바이트 표현이 다르다. `std::has_unique_object_representations_v<T>` (C++17) 가 true 인 타입만 바이트 단위 CRC/해시/비교(`memcmp`)가 값과 일치한다. 해결: 패딩 자리를 `rsvd` 멤버로 명시하고 0 으로 채우기, 또는 필드별로 직렬화. float 멤버도 false 다 (clang 확인, NaN 등 같은 값의 여러 표현).

</details>

## 9. 추가 연습

### 연습 1 — virtual 이 있는 클래스 재배치

`struct SensorBad { virtual void read(); u8 id; u64 ts; u8 flags; u32 val; };` 를 재배치해 크기를 줄여라. arm64 와 Cortex-M 각각의 크기는?

<details>
<summary>답 보기</summary>

큰 것부터: `{vptr; u64 ts; u32 val; u8 id; u8 flags;}` → arm64 **32 → 24**. Cortex-M 은 **24 → 24** 로 이득이 없다: AAPCS 에서 `uint64_t` 는 8바이트 정렬이라, vptr(4) 뒤에 4바이트 구멍이 생기고 (`ts @8`), 원래 배치도 `id @4, ts @8, flags @16, val @20` 으로 이미 24 였다. 레이아웃 최적화는 **타깃별로** 확인해야 하고, 그래서 `static_assert` 기대값을 `LP64 ? 32 : 24` 처럼 타깃별로 고정한다 (thumbv7em 덤프로 확인한 값).

</details>

### 연습 2 — compressed pair

`template<class A, class B> struct Pair { [[no_unique_address]] A a; [[no_unique_address]] B b; };` 에서 `Pair<Empty,Tag>`, `Pair<Empty,Empty>`, `Pair<Empty,uint32_t>` 의 크기는?

<details>
<summary>답 보기</summary>

1, **2**, 4. 서로 다른 빈 타입은 같은 주소에 겹칠 수 있지만, 같은 타입 두 개는 주소가 달라야 해서 b 가 오프셋 1 로 밀린다. 빈 타입과 `uint32_t` 는 겹쳐서 4. `std::unique_ptr<T, StatelessDeleter>` 가 포인터 1개 크기인 이유.

</details>

### 연습 3 — 꼬리 패딩 덮어쓰기 판별

`base_copy_would_clobber<Derived, Base>(d)` — 파생 멤버 `c` 가 베이스 `sizeof` 범위 안에 있으면 true.

<details>
<summary>답 보기</summary>

`addr_off(d, d.c) < sizeof(Base)`. `AfterNonPod` 는 `c @5 < 8` → true (덮어씀), `AfterPod` 는 `c @8` → false. memcpy 를 실제로 해 보는 대신 주소로 판정하는 이유: 그 memcpy 자체가 표준이 보장하지 않는 동작이기 때문.

</details>

### 연습 4 — CRC 해도 되는 타입

`concept CrcSafe` 를 정의하고 `CfgPadded{u32 magic; u8 ver, flags;}`, `CfgPacked{... u16 rsvd;}`, `CfgFloat{u32; float;}` 를 판정하라.

<details>
<summary>답 보기</summary>

`std::is_trivially_copyable_v<T> && std::has_unique_object_representations_v<T>`. `CfgPadded` ✗ (꼬리 2바이트 패딩), `CfgPacked` ✓ (같은 8바이트지만 패딩 자리가 멤버), `CfgFloat` ✗. 크기는 `CfgPadded` 와 `CfgPacked` 가 같다 — 차이는 "모든 바이트 값이 정해져 있는가" 다.

</details>

### 연습 5 — 접근 지정자와 standard-layout

`MixedAccess`(public a, private b)와 `AllPrivate`(a, b 모두 private + public getter)의 SL 여부와 크기는?

<details>
<summary>답 보기</summary>

`MixedAccess` ✗, `AllPrivate` ✓, 크기는 둘 다 8. "같은 접근 지정" 이 조건이지 "public" 이 조건이 아니다. 멤버 함수(getter)의 접근 지정은 무관.

</details>

### 연습 6 — 빈 베이스 두 개

`struct TwoEmptyBases : E1, E2 { uint32_t x; };` 의 크기와 SL 여부는?

<details>
<summary>답 보기</summary>

4, SL ✓ (clang/Itanium 에서 E1·E2 모두 오프셋 0 — 서로 다른 타입이라 겹칠 수 있다). MSVC 에서는 `__declspec(empty_bases)` 없이 8 이 될 수 있다는 점이 크로스 플랫폼 코드의 함정 (MSVC 는 직접 돌려 보지 못함 — 문서 기준).

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 18/18 passed
```

데모 파일 (하네스와 별개, 레슨 디렉터리에 있음):

```bash
c++ -std=c++20 -fsyntax-only -Xclang -fdump-record-layouts demo_layout.cpp                                # arm64 레이아웃 덤프
c++ -std=c++20 --target=thumbv7em-none-eabi -fsyntax-only -Xclang -fdump-record-layouts demo_layout.cpp   # Cortex-M4 레이아웃 덤프
c++ -std=c++20 -Wall -Wextra -Werror -fsyntax-only -DSHOW_ERROR demo_offsetof.cpp                         # offsetof/SL 에러 3개
```

## 10. 핵심 정리

- 빈 클래스 = 1바이트. 0바이트는 **베이스(EBO)** 또는 **`[[no_unique_address]]` 멤버**일 때만, 그리고 같은 타입끼리는 겹치지 않는다.
- `virtual` 하나 = 객체당 vptr 1개 (오프셋 0, 포인터 크기·정렬) → 모든 멤버가 밀리고 SL·TC·trivial 상실.
- standard-layout = 배치 보장 → `offsetof`, MMIO 오버레이, C 호환. 깨는 것: virtual, 접근 지정 섞기, 데이터 멤버가 베이스·파생 양쪽, 첫 멤버 타입 == 베이스.
- trivially copyable = memcpy·DMA·`bit_cast` 허용. trivial = 거기에 기본 생성자까지 trivial. 셋은 독립 축.
- non-POD 베이스의 꼬리 패딩은 재사용될 수 있다 (Itanium ABI) → 부분객체 memcpy 금지.
- 레지스터 블록: 비가상 메서드 OK, 예약 영역은 public 배열, `static_assert(is_standard_layout && offsetof && sizeof)` 로 고정.
- 포인터가 낀 크기는 `sizeof(void*)` 로 쓰고, 타깃별 값은 `clang --target=thumbv7em-none-eabi -Xclang -fdump-record-layouts` 로 확인.

## 참고자료

- C++ 표준 초안 (eel.is/c++draft): [intro.object] 객체 크기·주소, [class.prop] trivially copyable / trivial / standard-layout, [class.mem] 멤버 배치, [basic.types.general] memcpy 규칙, [support.types.layout] offsetof, [dcl.attr.nouniqueaddr] — https://eel.is/c++draft/
- cppreference — [Standard-layout / trivial 정리](https://en.cppreference.com/w/cpp/language/classes), [Empty base optimization](https://en.cppreference.com/w/cpp/language/ebo), [`[[no_unique_address]]`](https://en.cppreference.com/w/cpp/language/attributes/no_unique_address), [`offsetof`](https://en.cppreference.com/w/cpp/types/offsetof), [implicit-lifetime](https://en.cppreference.com/w/cpp/named_req/ImplicitLifetimeType)
- [Itanium C++ ABI §2.4 Non-POD class layout (vptr 위치, 꼬리 패딩 재사용)](https://itanium-cxx-abi.github.io/cxx-abi/abi.html#layout)
- [ARM — C++ ABI for the Arm Architecture (CPPABI32) / AAPCS32 정렬 규칙](https://github.com/ARM-software/abi-aa)
- P1847R4 *Make declaration order layout mandated* (C++23), P0593R6 *Implicit creation of objects* (C++20), P2327R1 *De-deprecating volatile compound operations* (C++23), P3247 *Deprecate the notion of trivial types* (C++26)
- ARM CMSIS-Core — 주변장치 `_TypeDef` 구조체와 `RESERVED` 멤버 관례
