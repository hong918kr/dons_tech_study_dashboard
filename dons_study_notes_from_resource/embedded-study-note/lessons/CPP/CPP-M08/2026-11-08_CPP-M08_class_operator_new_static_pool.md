---
id: CPP-M08
time: 80분
updated: 2026-11-08
---
# CPP-M08 클래스 전용 operator new + 정적 메모리 풀

## 학습 목표

- `new T` 가 **① 할당 함수 호출 → ② 생성자 실행** 두 단계임을 알고, 클래스 전용 `operator new` 로 ①만 정적 풀로 돌린다 — 호출부의 `new`/`delete` 문법은 그대로다.
- **고갈 때 nullptr 을 돌려주는 `operator new` 는 반드시 `noexcept`** 여야 한다는 규칙([expr.new])을 Cortex-M 어셈블리로 확인한다. noexcept 가 없으면 호출자의 `if (!m)` 까지 컴파일러가 지운다.
- 클래스 hook 의 경계를 안다: 이름 가림(placement·nothrow), `new T[n]` 은 안 잡힘, 큰 파생 클래스의 넘침, sized delete 의 크기는 어디서 오는가.
- 풀에서 이중 해제·범위 밖·블록 내부 포인터를 **상태를 바꾸지 않고** 거절하고, 왜 소멸자 안의 포이즌은 믿으면 안 되는지(lifetime DSE) 설명한다.
- ISR 할당 → 메인 해제 파이프라인에서 고갈을 드롭으로 처리하고, ELF 크기로 힙 대비 이득을 숫자로 말한다.

## 1. 문제

펌웨어 면접의 단골 꼬리질문이 "힙 없이 `new` 를 쓰게 할 수 있나? 고갈되면 어떻게 되나?"다. `new` 가 어떤 단계로 번역되는지 모르면 **고갈된 순간 주소 0 에 생성자를 돌리는 코드**를 쓰게 된다.

CPP-B14 는 힙을 **막는** 방법을 다뤘고, 이 레슨은 특정 타입의 `new` 를 **다른 곳으로 돌리는** 방법을 다룬다. 고정 블록 풀의 free list 알고리즘(블록 내장 링크, 하이워터마크, ASan 포이즌)은 DS-M02 소유다 — 여기서는 풀 코어를 주어진 코드로 받고, C++ 언어 쪽 연결부에 집중한다. C 라면 `msg_alloc()` + `msg_init(m)` 두 번을 부르는 자리를, C++ 은 `new Message(...)` 한 줄로 하되 그 두 단계 사이에 **컴파일러가 만드는 분기**가 있다.

```svg
<svg viewBox="0 0 750 318" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">new Message(id, src) 를 컴파일러가 번역한 모습</text>
  <rect x="10" y="40" width="230" height="52" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="22" y="62" fill="currentColor">① Message::operator new(28)</text>
  <text x="22" y="82" fill="currentColor" font-size="12">클래스 범위를 먼저 찾는다</text>
  <path d="M240 66 L290 66" stroke="currentColor" fill="none"/>
  <rect x="292" y="40" width="200" height="52" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="304" y="62" fill="currentColor">② null 검사?</text>
  <text x="304" y="82" fill="currentColor" font-size="12">noexcept 일 때만 생긴다</text>
  <path d="M492 66 L542 66" stroke="currentColor" fill="none"/>
  <rect x="544" y="40" width="196" height="52" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="556" y="62" fill="currentColor">③ Message::Message()</text>
  <text x="556" y="82" fill="currentColor" font-size="12">받은 주소에서 생성</text>
  <text x="10" y="128" fill="currentColor" font-weight="bold">풀이 비었을 때 (operator new 가 nullptr 반환)</text>
  <rect x="10" y="142" width="350" height="78" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="22" y="164" fill="currentColor">noexcept 없음: ② 가 없다</text>
  <text x="22" y="184" fill="currentColor" font-size="12">→ 주소 0 에서 생성자 실행 (UB, 맥에선 SIGSEGV)</text>
  <text x="22" y="204" fill="currentColor" font-size="12">→ 호출자의 if (!m) 도 최적화로 삭제됨</text>
  <rect x="390" y="142" width="350" height="78" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="402" y="164" fill="currentColor">noexcept 있음: ② 가 생긴다</text>
  <text x="402" y="184" fill="currentColor" font-size="12">→ 생성자를 건너뛰고 new-식의 값이 nullptr</text>
  <text x="402" y="204" fill="currentColor" font-size="12">→ 호출자의 if (!m) 이 정상 동작</text>
  <rect x="10" y="240" width="730" height="66" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="262" fill="currentColor">delete m : ~Message() → Message::operator delete(m, 28)</text>
  <text x="22" y="282" fill="currentColor" font-size="12">소멸자가 먼저, 그다음 반납. null 이면 소멸자를 건너뛰지만 operator delete 호출 여부는 미지정이므로</text>
  <text x="22" y="298" fill="currentColor" font-size="12">operator delete 는 nullptr 을 스스로 견뎌야 한다.</text>
</svg>
```

저장소는 28바이트 × 4블록 정적 배열이고, 메타데이터(`next_`, `live_`)는 블록 **밖**에 둔다. 인터럽트 마스크는 `CriticalSection` 가짜(mock)로 흉내 내고, 풀을 임계 구역 밖에서 건드리면 `irq::violations` 가 올라간다.

### Part A — new/delete 를 풀로 돌리기

- `Message::operator new(std::size_t)` — `g_pool` 에서 블록을 꺼낸다. 고갈이면 `nullptr`. **선언에 필요한 것을 추가**한다 (시작 코드의 선언은 일부러 틀렸다).
- `Message::operator delete(void*, std::size_t)` — sized 형태. `nullptr` 은 아무 일도 하지 않는다.
- 테스트: 네 번째까지 성공, 다섯 번째 `nullptr` + **생성자 미실행**(`ctor_count` 불변), 블록 간격 = `sizeof(Message)`, 지운 블록이 LIFO 로 재사용되고 생성자가 다시 초기화, 옆 블록 데이터 보존, `delete` nullptr 안전.

### Part B — 언어 hook 의 함정

- **이름 가림**: `operator new` 를 클래스에 선언하면 전역 판(`new (std::nothrow)`, `new (buf)`)이 전부 가려진다. nothrow 판을 클래스에 추가한다. `::new (buf) Message` 는 동작해야 한다.
- **배열**: `new Message[n]` 은 이 hook 을 타지 않는다. `operator new[]`/`operator delete[]` 를 `= delete` 해서 컴파일 에러로 막는다.
- **상속**: `BigMessage`(블록보다 큼)는 `operator new` 를 상속받는다. `sz > sizeof(Message)` 면 풀을 건드리지 않고 `size_rejects++` 후 `nullptr`. 크기가 같은 파생(`SameSizeMessage`)은 허용.
- **잘못된 해제**: `StaticPool::release` 에 검증을 넣는다 — 이중 해제 / 범위 밖 / 블록 내부 포인터를 상태 변화 없이 거절하고 이유를 돌려주며, 정상 반납이면 블록을 0xDD 로 채운다. `operator delete` 의 `sz` 가 크면 `wrong_size_frees++` 후 반납하지 않는다.

### Part C — ISR → 링 → 메인 루프

- `rx::uart_rx_isr(byte)`: `new (std::nothrow) Message` 로 만들고 `payload[0]`, `len = 1` 을 채워 링에 넣는다. `received` 는 항상 +1, 고갈이면 `dropped` +1 하고 끝.
- `rx::main_loop_once()`: 꺼내서 `sum` 에 더하고 `processed` +1, `delete`. `operator new`/`delete` 는 임계 구역 안에서 풀을 접근해야 한다.
- 고정 시드 xorshift32 로 20000회 할당/해제 스트레스: 모델(shadow)과 풀 상태가 항상 일치, 살아 있는 객체의 내용 보존, 살아 있는 블록끼리 주소 중복 없음.

## 2. Problem (English)

> **Route a class's dynamic allocation to a fixed-block static pool, with no heap.**
>
> Implement class-specific allocation for `Message` (28 bytes), backed by `StaticPool<sizeof(Message), alignof(Message), 4>`.
>
> 1. `new Message(id, src)` must take a block from the pool and run the constructor. When the pool is empty the new-expression must evaluate to `nullptr` **without running the constructor**. What must `operator new` be declared as for this to be well defined, and why?
> 2. `delete` must run the destructor and return the block; a freed block must be reused. `delete` of a null pointer must be harmless.
> 3. `new (std::nothrow) Message(...)` must work. Explain why `new (buffer) Message(...)` stops compiling once the class has its own `operator new`, and how to get placement new back.
> 4. Make `new Message[n]` a compile-time error. Without that, where does the array go?
> 5. A derived class larger than the block inherits `operator new`. Show the failure and guard against it. What size does a sized `operator delete` receive through a `Base*` with and without a virtual destructor?
> 6. Detect double free, out-of-range and interior pointers without corrupting the free list.
> 7. Use the pool from a mock UART RX ISR, dropping bytes when it is empty, with a critical section around each pool operation.
>
> Constraints: `-fno-exceptions -fno-rtti`, no heap, O(1) allocation.
>
> Example: with 4 blocks and 6 bytes arriving before the main loop runs, `received == 6`, `dropped == 2`, and the processed sum covers only the first 4 bytes.

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 대비
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
# 처음엔 "29/68 passed", exit 1 — TODO 를 채워 68/68 을 만든다
```

시작 코드의 `operator new` 는 크래시를 막으려고 `static` 임시 블록을 돌려준다. noexcept 없이 nullptr 을 돌려주면 하네스가 FAIL 대신 세그폴트로 죽기 때문이다 (왜 그런지가 이 레슨의 첫 주제다). 임시 블록은 답을 채우면서 지운다.

## 4. 힌트

<details>
<summary>힌트 1 — 첫 TODO 는 한 단어</summary>

`[expr.new]` 는 할당 함수가 **non-throwing exception specification** 을 가질 때만 null 반환을 실패 신호로 인정한다. 선언과 정의 양쪽에 같은 단어를 붙인다.

</details>

<details>
<summary>힌트 2 — 풀은 왜 클래스 안의 static 멤버가 아닌가?</summary>

클래스 본문 안에서 `sizeof(Message)` 는 불완전 타입이라 쓸 수 없다. 그래서 `g_pool` 은 클래스 뒤에 두고, `operator new` 정의도 그 뒤에 쓴다. `operator new` 는 `static` 을 안 적어도 static 멤버다([class.free]).

</details>

<details>
<summary>힌트 3 — nothrow 판은 새 로직이 필요 없다</summary>

`std::nothrow_t` 인자는 오버로드를 고르기 위한 표식일 뿐이다. 본체는 `Message::operator new(sz)` 위임 한 줄이면 된다.

</details>

<details>
<summary>힌트 4 — release 의 검사 순서</summary>

`nullptr` → 범위 (정수 비교) → 블록 경계 (`off % BlockSize`) → `live_[i]`. 인덱스를 계산하기 전에 범위와 경계부터 확인해야 `live_` 를 범위 밖으로 읽지 않는다. 상태 변경은 전부 통과한 뒤에.

</details>

<details>
<summary>힌트 5 — 크기 가드는 어디서?</summary>

`new BigMessage` 는 상속받은 `Message::operator new` 를 **`sz = sizeof(BigMessage)`** 로 부른다. 그 `sz` 를 비교하면 된다. 풀을 건드리기 전에.

</details>

<details>
<summary>힌트 6 — ISR 에서 쓸 때</summary>

`new (std::nothrow) Message(next_id++, 1)` 의 결과가 null 인지 **호출자가** 확인해야 한다. noexcept 는 new-식이 생성자를 건너뛰게 할 뿐, null 처리를 대신하지는 않는다. 링 용량이 풀 블록 수와 같으므로 할당에 성공하면 자리는 반드시 있다.

</details>

## 5. 풀이

### Part A — new-식의 두 단계와 null 검사

`new Message(...)` 는 `operator new` 호출과 생성자 호출로 쪼개지고, 그 사이에 null 검사가 들어갈지는 **할당 함수의 선언**이 정한다.

| 규칙 | 근거 |
|---|---|
| non-throwing 할당 함수가 실패하면 null 을 반환해야 한다 | [basic.stc.dynamic.allocation] |
| 그 외의 할당 함수가 실패하면 `std::bad_alloc` 으로 **throw 만** 가능 — null 반환은 계약 위반 | 같은 조 |
| null 을 받으면 "초기화를 하지 않고, 할당 해제 함수도 부르지 않으며 new-식의 값은 null" | [expr.new] |

컴파일러 입장에서 `noexcept` 없는 `operator new` 의 반환값은 **절대 null 이 아니다**. `-fno-exceptions` 라도 이 규칙은 바뀌지 않는다. `demo_nullcheck.cpp` (헤더 없음) — `pool_alloc()` 은 다른 TU 에 있다.

```cpp
struct Message {
    unsigned id;
    Message() : id(0xAA) {}
    static void* operator new(size_t) NE { return pool_alloc(); }
    ...
};
Message* make_checked() {
    Message* m = new Message;
    if (m == nullptr) oom();     // 호출자가 정성껏 검사
    return m;
}
```

Cortex-M4 (Apple clang 21, `--target=thumbv7em-none-eabi -O2 -fno-exceptions -fno-rtti`) 실측. `bash asm_targets.sh nullcheck`:

```text
=== noexcept 없음                       === -DUSE_NOEXCEPT
_Z12make_checkedv:                      _Z12make_checkedv:
    push    {r7, lr}                        push    {r4, lr}
    bl      _Z10pool_allocv                 bl      _Z10pool_allocv
    movs    r1, #170                        cbz     r0, .LBB1_2       ← new-식의 null 검사
    str     r1, [r0]      ← 그냥 쓴다        movs    r1, #170
    pop     {r7, pc}                        str     r1, [r0]
                                            pop     {r4, pc}
                                        .LBB1_2:
                                            mov     r4, r0
                                            bl      _Z3oomv           ← 호출자의 if 가 살아 있다
                                            mov     r0, r4
                                            pop     {r4, pc}
```

왼쪽에는 `oom()` 호출이 **아예 없다**. 호출자의 `if (m == nullptr)` 를 optimizer 가 "항상 거짓"으로 지웠다. 소스만 보면 검사하고 있는데 바이너리에는 없다. arm-none-eabi-g++ 14.2.1 (VM) 도 같은 결과다 (`cbz` 와 `_Z3oomv` 호출이 noexcept 일 때만 있음, 실측).

실제 실행 — `demo_crash.cpp` (블록 2개, 세 번째에서 고갈):

```text
$ c++ -std=c++20 -O2 -fno-exceptions -fno-rtti demo_crash.cpp -o demo_crash && ./demo_crash; echo "exit=$?"
new #0
  생성자 실행
  id=0
new #1
  생성자 실행
  id=1
new #2
Segmentation fault: 11  ./demo_crash
exit=139

$ c++ ... -DUSE_NOEXCEPT demo_crash.cpp ...
new #0
  생성자 실행
  id=0
new #1
  생성자 실행
  id=1
new #2
  고갈: nullptr 받음 — 정상 처리
exit=0
```

맥에서는 페이지 0 이 매핑돼 있지 않아 멤버 초기화에서 바로 SIGSEGV 가 난다 (생성자 본문의 출력 전에 죽었다). Cortex-M 에서는 주소 0 이 보통 플래시/부트 영역이어서 HardFault 나 BusFault 로 이어질 것으로 **예상**하지만, MPU 와 메모리맵에 따라 다르다 (미확인, 보드별). SRAM 을 0 번지로 리맵한 칩이라면 조용히 벡터 테이블을 덮는 더 나쁜 결과도 생각할 수 있다.

**경고에 기대면 안 된다**. 컴파일러는 `return nullptr;` 처럼 **리터럴**이 보일 때만 경고한다. 실측 (`-Wall -Wextra`):

| `operator new` 본문 | Apple clang 21 | arm-none-eabi-g++ 14.2.1 |
|---|---|---|
| `return nullptr;` | 경고 `-Wnew-returns-null` | 경고 "must not return NULL unless it is declared 'throw()'" |
| `void* p = nullptr; return p;` | 경고 없음 | 경고 없음 |
| `return pool_alloc();` (진짜 코드) | 경고 없음 | 경고 없음 |

컴파일러 옵션 `-fcheck-new` 는 noexcept 가 없어도 null 검사를 넣는다 (clang, GCC 둘 다 `cbz` 생성 확인). 그러나 표준 위반 코드를 플래그로 덮는 것이므로 `noexcept` 가 정답이다.

**delete 쪽**. `delete m` 은 null 이면 소멸자를 건너뛴다. 그러나 null 인 경우 `operator delete` 호출 여부는 **미지정**이고([expr.delete]), 사용자 정의 해제 함수가 `nullptr` 을 받을 수 있다. 그래서 `if (p == nullptr) return;` 이 필요하다. 하네스의 `Message::operator delete(nullptr, sizeof(Message))` 직접 호출이 이 경로다.

LIFO 재사용 테스트에서 주소를 `delete` 전에 정수로 저장한 이유: 해제된 포인터 값을 쓰는 건 구현 정의 동작이다([basic.stc]). 정수 사본이면 문제가 없다. 그리고 새로 얻은 블록은 `len == 0` 이다. 풀이 0xDD 로 채운 자리를 생성자가 다시 초기화했기 때문이다.

### Part B — 이름 가림과 배열, 상속

**이름 가림.** `new T` 의 할당 함수 이름은 **먼저 T 의 클래스 범위에서** 찾고, 찾으면 전역은 보지 않는다([expr.new], CPP-B14 참고). 실제 에러 (`demo_errors.cpp`):

```text
$ c++ -std=c++20 -fno-exceptions -fno-rtti -fsyntax-only -DSHOW_ERROR=3 demo_errors.cpp
demo_errors.cpp:29:14: error: no matching function for call to 'operator new'
   29 |     Msg* c = new (std::nothrow) Msg;      // 전역 nothrow 판도 가려진다
      |              ^   ~~~~~~~~~~~~~~
demo_errors.cpp:8:18: note: candidate function not viable: requires 1 argument, but 2 were provided
    8 |     static void* operator new(std::size_t) noexcept { return nullptr; }
      |                  ^            ~~~~~~~~~~~
1 error generated.

$ c++ ... -DSHOW_ERROR=2 demo_errors.cpp        # new (buf) Msg 도 같은 에러 (buf 쪽 인자)
```

해결: nothrow 는 클래스에 선언하고, placement 는 `::new (buf) T` 로 전역을 직접 지명한다 (placement new 의 수명 규칙은 CPP-M07). 표준 컨테이너는 `::new` 와 `std::allocator` 경로라서 이 hook 을 타지 않는다.

**배열.** 할당 함수 이름이 `operator new[]` 로 **다르다**. 클래스가 그것을 선언하지 않으면 조회가 전역 `operator new[]` 까지 내려간다. `demo_hooks.cpp` ① 실측:

```text
① new OnlySingle[3] 은 어디로?
  클래스 operator new 호출=0, 전역 operator new[] 호출=1
```

힙이 없는 제품이라면 링크 에러(CPP-B14)가 그나마 낫고, 힙이 있는 제품이면 조용히 풀 밖으로 나간다. `= delete` 로 막는다.

```text
demo_errors.cpp:23:14: error: call to deleted function 'operator new[]'
   23 |     Msg* a = new Msg[2];                  // 배열 new 는 = delete
```

주의 — 하네스의 첫 초안 `requires { new T[2]; }` 는 `Message` 에 대해 **가드 없이도 false** 였다. `Message` 에 기본 생성자가 없어서 식이 이미 ill-formed 이기 때문이다. 시작 코드의 실패 목록을 보고서야 알았다. 그래서 `new T[2]{T(a...), T(a...)}` 로 원소를 직접 만든다 — "컴파일 안 됨" 테스트는 **다른 이유로 통과**하기 쉽다. 가드가 없을 때 실패하는지 꼭 확인하라.

**상속.** `operator new` 는 static 멤버라 상속된다. 파생 크기를 모른 채 고정 블록을 주면 옆 블록을 덮는다. `demo_hooks.cpp` ②(가드 없는 `Unsafe`, 블록 16바이트, 파생 40바이트) 실측:

```text
② 블록보다 큰 파생 클래스 (가드 없음)
  Unsafe::operator new(sz=8)  블록=16
  Unsafe::operator new(sz=8)  블록=16
  옆 객체 second->tag 전: 0x1111
  Unsafe::operator new(sz=40)  블록=16
  옆 객체 second->tag 후: 0xbbbb   ← 덮였다
```

**ASan 이 이걸 못 잡았다** (`-fsanitize=address,undefined` 로도 같은 출력, 리포트 없음). 블록들이 **한 전역 배열** 안에 있어 redzone 이 없기 때문이다. ASan 으로 풀 내부를 지키려면 DS-M02 의 수동 poisoning 이 필요하다.

```svg
<svg viewBox="0 0 750 268" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">BigMessage 를 가드 없이 28 바이트 블록에 넣으면</text>
  <g stroke="currentColor">
    <rect x="20" y="42" width="170" height="44" class="f-ok"/>
    <rect x="190" y="42" width="170" height="44" class="f-ok"/>
    <rect x="360" y="42" width="170" height="44" class="f-mut"/>
    <rect x="530" y="42" width="170" height="44" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="30" y="68">block 0 (28B)</text>
    <text x="200" y="68">block 1: 다른 객체</text>
    <text x="370" y="68">block 2</text>
    <text x="540" y="68">block 3</text>
  </g>
  <rect x="20" y="100" width="550" height="34" class="f-warn" stroke="currentColor"/>
  <text x="30" y="122" fill="currentColor" font-size="12">new BigMessage (92B): 생성자가 block 0 ~ block 3 앞부분까지 씀</text>
  <text x="20" y="160" fill="currentColor" font-weight="bold">가드 두 겹</text>
  <rect x="20" y="172" width="340" height="82" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="32" y="194" fill="currentColor">런타임: operator new 의 sz 검사</text>
  <text x="32" y="214" fill="currentColor" font-size="12">if (sz &gt; sizeof(Message)) return nullptr;</text>
  <text x="32" y="234" fill="currentColor" font-size="12">상속을 허용해야 할 때</text>
  <rect x="390" y="172" width="340" height="82" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="402" y="194" fill="currentColor">컴파일: class Message final</text>
  <text x="402" y="214" fill="currentColor" font-size="12">error: base 'Sealed' is marked 'final'</text>
  <text x="402" y="234" fill="currentColor" font-size="12">상속이 필요 없다면 이게 제일 싸다</text>
</svg>
```

폴백을 어떻게 할지는 정책이다. Effective C++ 의 고전적 해법은 `if (sz != sizeof(Base)) return ::operator new(sz);` (전역 폴백)인데, 힙이 없는 펌웨어에서는 쓸 수 없어 `nullptr` 로 거절한다.

**sized delete 의 크기는 어디서 오나.** 표준의 답은 "delete-식이 넘긴다"이고, 값은 동적 타입에 달려 있다. `demo_hooks.cpp` ③ 실측 (`-DSHOW_UB` 포함):

```text
③ sized delete 의 sz
  sizeof NonVirt=4 NonVirtD=32 Virt=16 VirtD=40
  Virt::operator delete sz=40          ← Virt* 로 지웠는데 VirtD 크기: deleting destructor 가 넘겨 준다
  Virt::operator delete sz=16
  NonVirt::operator delete sz=32       ← 정적 타입 == 동적 타입 (정상)
  NonVirt::operator delete sz=4        ← NonVirt* 로 NonVirtD 를 지움: UB, 잘못된 크기
```

마지막 줄은 [expr.delete] 의 UB(virtual 소멸자 없이 base 포인터로 파생을 delete)이고 `-DSHOW_UB` 로만 켠다. 풀에서는 `sz` 가 맞지 않는 곳에 블록을 돌려줄 수 있는 지점이다. 다음도 실측이다:

| 상황 | 결과 |
|---|---|
| 클래스에 `(void*)` 와 `(void*, size_t)` 둘 다 선언 | `(void*)` 판 선택 (④ 출력 `Both::operator delete(void*)`) — 클래스 범위 규칙 [expr.delete] |
| `-fno-sized-deallocation` | 클래스 sized delete 는 그대로 동작 (같은 `sz=40` 출력). 그 플래그는 전역 sized delete 만 대상 |

**잘못된 해제.** `release` 의 검증은 free list 를 건드리기 **전에** 끝나야 한다. 표는 입력별 결과.

| 입력 | 판별 | 결과 | `used_` / free list |
|---|---|---|---|
| `nullptr` | 포인터 비교 | `Null` | 불변 |
| 풀 밖 | `a - b >= N * BlockSize` (정수) | `OutOfRange`, `bad_frees_++` | 불변 |
| 블록 중간 | `off % BlockSize != 0` | `Interior`, `bad_frees_++` | 불변 |
| 이미 빈 블록 | `!live_[i]` | `DoubleFree`, `double_frees_++` | 불변 |
| 정상 | 위를 전부 통과 | `Ok`, 0xDD 포이즌, free list 앞에 삽입 | 변경 |

범위 비교를 정수로 하는 이유: 서로 다른 객체의 포인터를 `<` 로 비교한 결과는 미지정이다([expr.rel]). 그리고 메타데이터가 **블록 밖**(`next_`, `live_`, 합쳐 블록당 3바이트)에 있으므로, 해제 후 객체에 쓰는 버그가 free list 를 망가뜨릴 수 없다. DS-M02 의 "블록 내장 링크"(블록당 0바이트)와의 트레이드오프다.

이중 `delete` 는 언어 수준에서 이미 **소멸자가 두 번** 돈 뒤다. 풀이 `operator delete` 에서 알아채도 소멸자의 부작용은 이미 일어났다. 그래서 하네스는 두 번째를 `Message::operator delete` 직접 호출로 시험한다.

**소멸자 포이즌은 믿지 말 것 (lifetime DSE).** 흔한 디버그 코드가 `~Message() { magic = 0xDEAD; }` 다. 또 흔한 생각이 "`operator new` 에서 0 으로 채웠으니 생성자는 안 채워도 된다"이다. 둘 다 표준상 소용없다: 소멸자 끝 이후와 생성자 시작 이전의 객체 내용은 볼 수 없다([basic.life]). `demo_lifetime_dse.cpp` 의 GCC 실측 (`arm-none-eabi-g++ -O2 -mcpu=cortex-m4 -mthumb`):

```text
기본 (-flifetime-dse=2)                 -fno-lifetime-dse
_Z10read_freshv:                        _Z10read_freshv:
    ldr  r3, .L3                            ldr  r3, .L3
    ldr  r0, [r3]   ← memset 이 사라짐      movs r0, #0
    bx   lr                                 str  r0, [r3]
                                            str  r0, [r3, #4]
                                            bx   lr
_Z4killP8Poisoned:                      _Z4killP8Poisoned:
    cbz  r0, .L5                            cbz  r0, .L5
    b    _Z9pool_freePv  ← 0xDEAD 없음      movw r2, #57005
                                            str  r2, [r0]
                                            b    _Z9pool_freePv
```

같은 파일을 Apple clang 21 으로 컴파일하면 memset(`str r2,[r1]`)과 0xDEAD 쓰기가 **둘 다 남는다** (실측). 그러니 "내 컴파일러에서는 됐다"가 다른 컴파일러에서 무너지는 전형이다. 이 레슨의 풀은 소멸자 **다음** `release` 안에서, 객체 수명이 끝난 뒤의 raw 저장소에 포이즌을 쓴다 — 이 쓰기는 표준상 유효하다.

### Part C — ISR 파이프라인과 비용

흐름은 `uart_rx_isr` → `new (nothrow)` → 링 → `main_loop_once` → `delete` 다. 할당과 해제는 서로 다른 실행 문맥이므로 풀 접근은 임계 구역이 필요하다. 실제 칩이라면 `CriticalSection` 은 PRIMASK 저장 → `cpsid i` → 복원이고, 이미 마스크된 ISR 안에서도 안전하도록 **저장/복원** 형태여야 한다 (CPP-B06 참고). 생성자는 임계 구역 **밖**에서 돈다 — 마스크 구간을 할당 자체로만 좁힌다.

| ISR 관점 | 결과 |
|---|---|
| 풀 고갈 | `nullptr` → `dropped++`, 즉시 리턴 (블로킹·abort 금지) |
| 링 용량 | 풀 블록 수와 동일 → 할당 성공이면 자리 있음 (별도 full 검사 불필요) |
| 소멸 | 메인 루프에서만 → ISR 은 짧게 유지 |

**결정성 확인.** `bash asm_targets.sh pool` 의 `Message::operator new` (Cortex-M4, Apple clang 21) 어셈블리에서 **되돌아가는 분기가 없다**. 재사용 경로는 손으로 세어 37 명령이다 (mock 카운터 포함, 일반 변수라 깊이 증감의 store 는 컴파일러가 합쳤다 — 실제 PRIMASK 는 `mrs`/`cpsid`/`msr` 로 바뀐다). `operator delete` 쪽에서 뒤로 가는 `b .LBB2_9` 는 공통 에필로그로 가는 점프라 루프가 아니다. 다만 인라인된 `release` 의 `bl memset`(고정 28바이트) 안에는 루프가 숨어 있고, **블록 크기로 상한**이 정해져 있다. 나눗셈은 `off % 28` 이 곱셈 시퀀스로 바뀌어 사라졌다(`muls`).

**ELF 크기 (링크까지 한 실측, VM 의 arm-none-eabi-g++ 14.2.1, `demo_size.cpp`, `-fno-exceptions -fno-rtti --gc-sections`):**

```text
   text    data     bss     dec     hex filename
    128       4      40     172      ac pool.elf
  14893    1356     420   16669    411d heap.elf
```

| | pool.elf | heap.elf (전역 `new`) |
|---|---|---|
| `.text` | **128 B** | 14893 B |
| 끌려온 심볼 (`nm -C`, 일부) | Message 관련 코드뿐, malloc·operator new 없음 | `operator new(unsigned int)`, `_malloc_r`(0x58E), `_free_r`, `__cxa_allocate_exception`, `std::bad_alloc` vtable, `__gxx_personality_v0`(0x474), unwinder |

앱은 `-fno-exceptions` 인데 예외 지원 코드가 들어왔다 — **libstdc++ 의 `operator new` 는 라이브러리 안에서 이미 예외를 던지도록 빌드**되어 있기 때문이다. `new` 를 클래스 풀로 돌리는 것은 지연시간뿐 아니라 이미지 크기 문제이기도 하다. 힙을 쓴다고 항상 14 KB 는 아니다(libc 설정, nano specs 에 따라 달라짐 — 이 숫자는 이 빌드 조건에서의 측정값).

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution   # 68/68 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer solution.cpp -o solution_san && ./solution_san   # 68/68, 리포트 없음

# 데모 (맥)
c++ -std=c++20 -O2 -fno-exceptions -fno-rtti demo_crash.cpp -o demo_crash && ./demo_crash   # exit=139
c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -DUSE_NOEXCEPT demo_crash.cpp -o demo_crash && ./demo_crash   # exit=0
c++ -std=c++20 -O2 -fno-exceptions -fno-rtti demo_hooks.cpp -o demo_hooks && ./demo_hooks
c++ -std=c++20 -fno-exceptions -fno-rtti -fsyntax-only -DSHOW_ERROR=1 demo_errors.cpp       # 1~5
bash asm_targets.sh nullcheck    # asm_targets.sh pool 은 ARM_INC 헤더 필요 (없으면 VM 에서 복사)
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `static void* operator new(size_t) { return pool.alloc(); }` | null 을 돌려줄 수 있는데 `noexcept` 없음 → 고갈 때 null 에서 생성자 실행, 호출자의 null 검사 삭제 | `static void* operator new(size_t) noexcept` |
| "`-fno-exceptions` 니까 어차피 nullptr" | 플래그는 생성 코드에 null 검사를 넣지 않는다. 실측: 검사 없음 | 선언에 `noexcept` |
| 경고가 없으니 안전 | 컴파일러는 리터럴 `return nullptr` 만 경고. 함수 경유는 조용함 | 코드 리뷰 + `noexcept` 존재 확인 (`static_assert(noexcept(T::operator new(1)))`) |
| `operator delete(void*)` 에 null 처리 없음 | null 호출 여부는 미지정, 직접 호출도 가능 | `if (!p) return;` |
| `new Message[n]` 을 방치 | 전역 `operator new[]` 로 빠져나감 | `operator new[]` / `delete[]` `= delete` |
| 클래스에 `operator new` 만 넣고 `new (buf) T` | 전역 placement 가 가려져 컴파일 에러 | `::new (buf) T` |
| 파생 클래스 크기 확인 안 함 | 상속받은 hook 이 블록보다 큰 객체를 받음 → 옆 블록 덮음 | `sz` 검사 또는 `final` |
| 소멸자에서 `magic = 0xDEAD` 로 해제 표시 | lifetime DSE 로 GCC 가 store 를 지운다 (실측) | `release` 안에서 raw 저장소에 포이즌 |
| "operator new 가 0 으로 채웠으니 멤버 초기화 불필요" | 생성자 시작 시 내용은 불확정. GCC 가 memset 을 지운다 (실측) | 생성자에서 멤버 초기화 |
| 범위 검사를 `p < begin` 포인터 비교로 | 서로 다른 객체 포인터 비교는 미지정 | `uintptr_t` 로 비교 |
| 클래스 안에 `static StaticPool<sizeof(Message),...>` | 그 지점에서 `Message` 는 불완전 타입 | 클래스 뒤에 `inline constinit` |

## 8. 면접 Q&A

<details>
<summary>Q1. 고갈될 때 nullptr 을 돌려주는 operator new 를 쓰고 싶습니다. 무엇을 조심해야 합니까?</summary>

`noexcept` 로 선언해야 합니다. [expr.new] 에서 null 반환을 실패로 인정하는 것은 non-throwing 할당 함수뿐이고, 그래야 컴파일러가 생성자 앞에 null 검사를 넣습니다. 없으면 생성자가 null 에서 돌고, 호출자의 `if (!p)` 는 "new 는 null 을 안 돌려준다"는 가정으로 삭제됩니다 (이 레슨의 Cortex-M4 asm 에서 `oom()` 호출이 통째로 사라짐). `-fcheck-new` 로 우회할 수는 있지만 플래그에 의존한다는 뜻입니다.

꼬리질문: "-fno-exceptions 에서 throw 가 없으니 다르지 않나요?" → 규칙은 예외 명세로 결정되고 `-fno-exceptions` 와 무관합니다. 실측으로 검사 코드가 생기지 않습니다.

</details>

<details>
<summary>Q2. 클래스 전용 operator new 와 전역 operator new 교체는 어떻게 다릅니까?</summary>

범위가 다릅니다. 클래스 전용은 그 타입의 `new T` 만 영향을 받고, 다른 할당과 무관합니다. 전역 교체(CPP-B14)는 프로그램 전체의 `::operator new` 를 바꿉니다. 클래스 전용은 전역 판을 **가리기** 때문에 `new (std::nothrow) T` / `new (buf) T` 가 함께 깨집니다. 그리고 `std::vector<T>` 는 `std::allocator` → `::operator new` 경로로 클래스 hook 을 **타지 않습니다** — 컨테이너까지 잡으려면 allocator 또는 pmr 쪽 문제입니다.

</details>

<details>
<summary>Q3. new Message[3] 은 클래스의 operator new 로 옵니까?</summary>

아닙니다. 이름이 `operator new[]` 라서 그걸 클래스에 선언하지 않으면 전역 `operator new[]` 로 갑니다 (`demo_hooks.cpp`: 클래스 호출 0, 전역 호출 1). 고정 블록 풀에 맞지 않으니 `operator new[]` / `operator delete[]` 를 `= delete` 해서 컴파일 에러로 막습니다. 배열이 필요하면 `std::array<Message, 8>` 을 객체로 두거나 정적 배열이 더 맞습니다.

</details>

<details>
<summary>Q4. sized delete 의 크기는 어디서 옵니까? 풀에서 믿어도 됩니까?</summary>

delete-식이 넘깁니다. virtual 소멸자가 있으면 deleting destructor 가 동적 타입 크기를, 없으면 정적 타입 크기를 넘깁니다 (`demo_hooks.cpp` 에서 `Virt*` → `sz=40`). 다만 virtual 소멸자 없이 base 포인터로 파생을 지우면 UB 이고 실측상 base 크기(`sz=4`)가 왔습니다. 그래서 크기는 **검증 수단**으로 쓰고, 풀의 인덱스 계산에는 포인터 자체를 씁니다. 또 클래스에 `(void*)` 와 `(void*, size_t)` 를 둘 다 선언하면 `(void*)` 가 선택되어 크기가 안 옵니다 — 하나만 두십시오.

</details>

<details>
<summary>Q5. 이중 해제를 어떻게 감지합니까? 비용은요?</summary>

블록 인덱스별 상태(`live_[i]`)를 **블록 밖**에 두고, `release` 에서 범위·경계·상태를 통과한 뒤에만 free list 를 고칩니다. 블록당 1바이트(비트맵이면 1/8바이트), O(1). 블록 내장 free list 만 쓰면(DS-M02 기본형) 해제된 블록의 첫 워드가 다음 포인터로 쓰이므로 use-after-free 에 약하고, 상태 표식도 별도로 필요합니다. 한계 — 이미 재할당된 블록의 stale 포인터로 `delete` 하면 `live_` 는 true 이므로 잡지 못합니다. 그건 세대 번호(generation) 를 담은 핸들로 풀어야 합니다.

</details>

<details>
<summary>Q6. ISR 에서 new 를 호출해도 됩니까?</summary>

조건부로 됩니다. `operator new` 가 ① O(1) 이고 ② 잠금이 대기하지 않고(임계 구역만) ③ 고갈을 `nullptr` 로 알리며 ④ 생성자도 짧고 블로킹이 없어야 합니다. 일반 힙 `malloc` 은 ①②가 깨지므로 금지입니다. 또 임계 구역은 저장/복원 형태여야 중첩 ISR 에서 인터럽트를 잘못 켜지 않습니다.

</details>

<details>
<summary>Q7. noexcept operator new 인데 생성자가 throw 하면요?</summary>

별개의 얘기입니다. `noexcept` 는 할당 함수의 명세이지 new-식 전체나 생성자의 명세가 아닙니다. 예외가 켜진 빌드에서 생성자가 던지면 **짝이 맞는 해제 함수**가 불려 저장소를 회수합니다 ([expr.new]) — placement 인자를 받는 판이면 `operator delete(void*, Prio)` 처럼 같은 인자의 판이 필요하고, 찾지 못하면 저장소가 샙니다. 이 레슨의 `-fno-exceptions` 빌드에서는 해당하지 않습니다. 예외 전반은 MCPP-M10 참고.

</details>

<details>
<summary>Q8. 소멸자에서 magic 을 지워 use-after-free 를 잡으려는데 왜 안 됩니까?</summary>

소멸자가 끝나면 객체의 수명이 끝나고 그 쓰기는 관측 불가능하므로, GCC 는 `-flifetime-dse`(기본 활성)로 해당 store 를 지웁니다. arm-none-eabi-g++ 14.2.1 에서 `-fno-lifetime-dse` 를 줄 때만 0xDEAD 쓰기가 나타났습니다. 수명 밖에서 쓰고 싶으면 풀이 `operator delete` 안에서 raw 저장소에 포이즌을 써야 합니다.

</details>

## 9. 추가 연습

### 연습 1 — 타입마다 자기 풀: `PoolAllocated<T, N>` 믹스인

`struct Frame : PoolAllocated<Frame, 2>` 처럼 쓰는 재사용 믹스인을 만든다. `sz == sizeof(T)` 만 허용.

<details>
<summary>답</summary>

함정: base 절 시점에 `T` 는 불완전 타입이므로 멤버 선언에 `sizeof(T)` 를 쓸 수 없다. 함수 안으로 미룬다.

```cpp
static auto& pool() noexcept {
    static constinit StaticPool<sizeof(T), alignof(T), N> p;   // 지역 static + constinit: guard 불필요
    return p;
}
static void* operator new(std::size_t sz) noexcept { return sz == sizeof(T) ? pool().alloc() : nullptr; }
```

CRTP 자체의 설명은 CPP-M05. `T` 가 달라지면 인스턴스가 달라지므로 타입별로 독립된 풀이다.

</details>

### 연습 2 — 과정렬 타입 (`alignas(32)` DMA 디스크립터)

`alignas(32) struct DmaDesc` 를 풀에 올리고 반환 주소가 32의 배수임을 확인한다.

<details>
<summary>답</summary>

`__STDCPP_DEFAULT_NEW_ALIGNMENT__` 보다 큰 정렬이면 new-식은 먼저 `operator new(size_t, std::align_val_t)` 를 찾는다. 클래스에 그 판이 없으면 정렬 인자 없는 판으로 내려온다([expr.new]). 그러니 저장소 쪽 `alignas(BlockAlign)` 이 책임져야 한다. 믹스인에서는 `alignof(T)` 가 그대로 들어가므로 해결된다.

</details>

### 연습 3 — 우선순위 예비 풀 (클래스 전용 placement 인자)

`new (Prio::High) Packet` 은 일반 풀이 비면 예비 풀까지 쓰고, `Prio::Normal` 은 예비를 건드리지 않게 한다.

<details>
<summary>답</summary>

`static void* operator new(std::size_t, Prio) noexcept` 처럼 추가 인자를 받는 판을 선언한다. placement 형식만 선언하면 `new Packet`(인자 없음)은 컴파일 에러가 된다 — 호출부에 우선순위를 강제하는 효과. 해제는 어느 풀인지 `contains()` 로 판별한다.

</details>

### 연습 4 — `std::make_unique` 와 풀

`std::make_unique<Job>(7)` 이 풀을 쓰는지, 고갈 시 어떻게 되는지 확인한다. `sizeof(std::unique_ptr<Job>)` 는?

<details>
<summary>답</summary>

`make_unique` 는 `new T(...)` 로 정의되어 있으므로 클래스 hook 을 탄다. 고갈이면 빈 `unique_ptr` 이 나온다 (`operator new` 가 noexcept 일 때만 안전). 기본 삭제자라 `sizeof` 는 포인터 1개 — 삭제자가 `delete` 를 부르므로 custom deleter 없이도 풀로 돌아간다.

</details>

### 연습 5 — 전역 초기화 안에서 new

`Early* g_early = new Early;` 가 전역 초기화 안에서 동작하려면 풀이 무엇을 만족해야 하나?

<details>
<summary>답</summary>

풀이 **constant initialization** 돼야 다른 TU 의 동적 초기화 순서와 무관하게 이미 준비되어 있다(CPP-B09). 이 레슨의 `StaticPool` 은 `constexpr` 생성자 + `constinit` 이고, free list 도 `fresh_` 로 lazy 하게 만들어 `.bss` 에 들어갈 수 있다. 같은 TU 의 테스트는 순서를 증명하지 못한다 — 증명은 `constinit` 이 컴파일된다는 사실 자체다.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 24/24 passed
```

## 10. 핵심 정리

- `new T` = 할당 함수 + 생성자. 클래스에 `operator new` 를 선언하면 호출부를 안 고치고 ①만 교체된다.
- 고갈에 nullptr 을 돌려주려면 **`noexcept` 필수**. 없으면 생성자가 null 에서 돌고 호출자의 `if (!m)` 도 삭제된다 (`-fno-exceptions` 와 무관).
- 경고는 리터럴 `return nullptr` 만 잡는다. 함수 경유는 조용하다.
- 클래스 `operator new` 는 전역 판을 **가린다** → nothrow 판은 클래스에 선언, placement 는 `::new`.
- `new T[n]` 은 hook 을 안 탄다 → `operator new[]` `= delete`. 상속된 hook 은 큰 파생을 받는다 → `sz` 검사 또는 `final`.
- sized delete 의 `sz` 는 delete-식이 넘긴다 (virtual 소멸자면 동적 타입). 클래스에 두 판을 다 두면 `(void*)` 가 선택된다.
- 해제 검증은 상태 변경 **전**에: 범위 → 경계 → `live_`. 메타데이터는 블록 밖. 소멸자/operator new 안의 포이즌·제로 필은 lifetime DSE 로 사라진다.
- 실측 (Cortex-M4 ELF): 풀 `.text` 128 B vs 전역 `new` 14893 B — `operator new` 는 `-fno-exceptions` 여도 라이브러리의 예외 코드를 끌고 온다.

## 참고자료

- C++20 표준: [expr.new], [expr.delete], [class.free], [basic.stc.dynamic.allocation], [basic.stc.dynamic.deallocation], [basic.life], [expr.rel], [new.delete.placement]
- [cppreference — new expression](https://en.cppreference.com/w/cpp/language/new) · [operator new](https://en.cppreference.com/w/cpp/memory/new/operator_new) · [operator delete](https://en.cppreference.com/w/cpp/memory/new/operator_delete) · [delete expression](https://en.cppreference.com/w/cpp/language/delete)
- [cppreference — std::make_unique](https://en.cppreference.com/w/cpp/memory/unique_ptr/make_unique) · [constinit](https://en.cppreference.com/w/cpp/language/constinit)
- Scott Meyers, *Effective C++* 3판 Item 49~52 (operator new/delete 관례)
- [GCC C++ Dialect Options](https://gcc.gnu.org/onlinedocs/gcc/C_002b_002b-Dialect-Options.html) — `-fcheck-new`, `-flifetime-dse`, `-fsized-deallocation`
- [Clang Diagnostics — -Wnew-returns-null](https://clang.llvm.org/docs/DiagnosticsReference.html#wnew-returns-null)
- 관련 레슨: DS-M02(풀 알고리즘·ASan 포이즌) · CPP-B14(힙 금지) · CPP-M07(placement new 수명) · CPP-B09(정적 초기화) · CPP-B06(RAII 가드) · CPP-M05(CRTP) · MCPP-M10(예외)
