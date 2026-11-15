---
id: CPP-A02
time: 110분
updated: 2026-11-11
---
# CPP-A02 ISR과 C++ 객체 연결: 정적 트램펄린 디스패치

## 학습 목표

- 벡터 테이블 엔트리(1워드, C 링크 함수 포인터)에서 C++ 드라이버 인스턴스의 멤버 함수까지 가는 두 경로를 구현한다: **런타임 등록 테이블** (`extern "C"` 트램펄린 하나 + IPSR → `{thunk, obj}`) 과 **컴파일 타임 바인딩** (`Trampoline<IRQ, 인스턴스, 멤버>::isr` 를 테이블에 직접).
- 표준이 말하는 것과 컴파일러가 하는 것을 구분해 말한다: 템플릿은 C 링크를 가질 수 없고 ([temp.pre]/6), 언어 링크가 다른 함수 타입은 다른 타입이지만 ([dcl.link]/1) Apple clang 21 과 arm-none-eabi-g++ 14.2 는 같은 타입으로 취급한다 (실측).
- 등록 테이블의 세 가지 함정을 테스트로 잡는다: 두 워드 쓰기 사이에 끼어드는 IRQ, 등록과 NVIC enable 의 순서, 소멸된 드라이버가 남긴 바인딩. 등록 안 된 IRQ 는 기본 핸들러가 기록하고 그 IRQ 를 꺼서 폭주를 막는다.
- 실제 thumbv7em 출력으로 비용을 말한다: 트램펄린 경로 12 명령 + 간접 분기 vs 컴파일 타임 경로 3 명령, 테이블 RAM 768 B vs 플래시 448 B, `constinit` 이 없으면 벡터 테이블이 0 으로 시작할 수 있는 이유.
- weak alias 기본 핸들러와 `.isr_vector` 섹션을 C++ 로 정의하고, `extern "C"` 를 빼먹으면 **링크 에러 없이** 핸들러가 사라지는 것을 링크한 ELF 로 보인다.

## 1. 문제

"UART 드라이버를 클래스로 만들었는데 인터럽트 핸들러는 어떻게 연결하죠? `USART1_IRQHandler` 안에서 전역 객체를 부르면 인스턴스가 여러 개일 때 어떻게 하나요?" — C++ 로 펌웨어를 쓰는 팀의 면접에서 거의 반드시 나오는 질문이다. 벡터 테이블은 **인자 없는 C 함수 주소** 의 배열이고, 멤버 함수는 `this` 가 필요하다. 그 사이를 잇는 작은 함수가 **트램펄린(trampoline)** 이다. 답은 쉬워 보이지만 꼬리질문이 깊다: 어디에 객체 포인터를 두나, 등록 중에 IRQ 가 오면, 객체가 죽으면, 비용은, 표준적으로 맞나.

이미 배운 것은 다시 하지 않는다: 객체 포인터 + 스텁 함수 포인터 delegate (CPP-M12), C 의 함수 포인터 테이블 (C-M06), NVIC ISER/ICER/IPR 비트 계산 (BM-M01), 벡터 테이블을 RAM 으로 복사하고 VTOR 를 바꾸기 (BM-A01), PRIMASK RAII 가드 (CPP-B06), ISR 공유 변수와 lock-free (CPP-A04). 이 레슨은 그 조각들을 **ISR 진입 경로** 하나로 엮는다.

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">IRQ 37 (USART1) 이 들어왔을 때 — 두 디스패치 경로</text>
  <rect x="10" y="40" width="170" height="54" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="20" y="62" fill="currentColor">하드웨어 예외 진입</text>
  <text x="20" y="82" fill="currentColor" font-family="monospace" font-size="12">IPSR=53, PC=VTOR[53]</text>
  <path d="M180 67 L215 67 L215 120 L230 120" stroke="currentColor" fill="none"/>
  <path d="M215 67 L215 235 L230 235" stroke="currentColor" fill="none"/>
  <text x="240" y="102" fill="currentColor" font-weight="bold">(1) RAM 테이블 + 런타임 등록</text>
  <rect x="230" y="108" width="150" height="40" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="238" y="125" fill="currentColor" font-family="monospace" font-size="12">irq_trampoline</text>
  <text x="238" y="141" fill="currentColor" font-size="12">extern "C", 하나뿐</text>
  <path d="M380 128 L400 128" stroke="currentColor"/>
  <rect x="400" y="108" width="150" height="40" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="408" y="125" fill="currentColor" font-family="monospace" font-size="12">g_slots[53-16]</text>
  <text x="408" y="141" fill="currentColor" font-family="monospace" font-size="12">{fn, obj} RAM</text>
  <path d="M550 128 L570 128" stroke="currentColor"/>
  <rect x="570" y="108" width="160" height="40" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="578" y="125" fill="currentColor" font-family="monospace" font-size="12">member_thunk&lt;Uart,</text>
  <text x="578" y="141" fill="currentColor" font-family="monospace" font-size="12">  &amp;Uart::on_irq&gt;(obj)</text>
  <text x="240" y="170" fill="currentColor" font-size="12">clang thumbv7em: mrs ipsr → 범위 검사 → ldr fn, ldr obj → bx fn → b on_irq</text>
  <text x="240" y="188" fill="currentColor" font-size="12">= 12 명령, 간접 분기 1 + 직접 분기 1, 데이터 로드 2 (실측)</text>
  <text x="240" y="218" fill="currentColor" font-weight="bold">(2) 플래시 테이블 + 컴파일 타임 바인딩</text>
  <rect x="230" y="225" width="320" height="40" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="238" y="242" fill="currentColor" font-family="monospace" font-size="12">Trampoline&lt;37, g_uart1, &amp;Uart::on_irq&gt;::isr</text>
  <text x="238" y="258" fill="currentColor" font-size="12">정적 멤버 함수 (C++ 링크) 를 테이블에 직접</text>
  <path d="M550 245 L570 245" stroke="currentColor"/>
  <rect x="570" y="225" width="160" height="40" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="578" y="250" fill="currentColor" font-family="monospace" font-size="12">g_uart1.on_irq()</text>
  <text x="240" y="287" fill="currentColor" font-size="12">clang: movw/movt r0,&amp;g_uart1; b on_irq = 3 명령, 데이터 로드 0, 조회 없음</text>
  <text x="240" y="307" fill="currentColor" font-size="12">대가: 인스턴스·IRQ 가 빌드 때 정해져야 한다 (교체·플러그인 불가)</text>
</svg>
```

### Part A — 런타임 등록 테이블과 트램펄린 하나

- `IrqRegistry::attach_raw(irq, thunk, obj)` / `detach(irq)`: 범위 밖, null thunk, 이미 주인이 있는 IRQ 는 `false`. 쓰기는 주어진 `store_binding()` (두 워드를 따로 쓰고 사이에 틈) 으로 한다.
- `IrqRegistry::dispatch(irq)`: 등록돼 있으면 `fn(obj)`, 아니면 `unhandled_irq(irq)` (기록 + 그 IRQ disable).
- `extern "C" void irq_trampoline(void)`: 모든 외부 IRQ 벡터가 이 함수 하나를 가리킨다. 지금 처리 중인 IRQ 번호는 IPSR 에서 얻는다.
- 타입 있는 `attach<&Uart::on_irq>(irq, uart)` 는 주어진다 — 임시 객체와 const 객체는 컴파일 에러 (requires-expression 으로 증명).

### Part B — 경쟁, 순서, 수명, 그리고 컴파일 타임 바인딩

- `IrqMaskGuard`: 그 IRQ 하나만 NVIC 에서 잠시 끄고 **원래 상태로** 복원한다. `attach_raw`/`detach` 가 이걸로 두 워드 쓰기를 감싸서, 틈에 같은 IRQ 가 와도 `{새 fn, 옛 obj}` 를 보는 ISR 이 없어야 한다.
- `Uart::start(reg)` / `stop()`: 등록과 NVIC enable 의 순서, 그 역순. 소멸자가 `stop()` 을 부른다 (RAII).
- `Trampoline<N, Inst, M>`: `requires (trampoline_ok(N) && IrqMember<M, T>)` 와 `static void isr()`.

### Part C — 벡터 테이블 두 개로 같은 결과

- `has_duplicate_irq<Tr...>()` 와 `consteval make_flash_table<Tr...>()` — 전부 `Default_Handler` 로 채우고 각 `Tr::isr` 를 `16 + Tr::irq` 에.
- UART 4개 (USART1/2/3, UART4) 에 xorshift 로 2만 번 가짜 IRQ: 3/4 는 수신 바이트, 1/4 는 아무 IRQ 번호 (등록 안 된 것 포함). RAM 테이블(등록)과 플래시 테이블(Trampoline) 이 인스턴스별 수신 수·체크섬·spurious·미처리 수까지 **같은 결과** 여야 한다.

## 2. Problem (English)

> **Connect Cortex-M interrupt vectors to C++ driver instances.**
>
> The vector table holds `extern "C"` `void(void)` function addresses; drivers are objects with `void on_irq()` members. Implement both:
>
> 1. A runtime registry: `bool attach_raw(unsigned irq, void(*thunk)(void*), void* obj)`, `bool detach(unsigned irq)`, `void dispatch(unsigned irq) const`, and one `extern "C" void irq_trampoline(void)` that recovers the IRQ number from IPSR. Unregistered IRQs go to a default handler that logs and disables the IRQ. One owner per IRQ.
> 2. Registration must be safe against the same IRQ firing between the two word stores of a binding. Driver `start()`/`stop()` must order registration and NVIC enable correctly, and a destroyed driver must never be called.
> 3. A compile-time binding `template<unsigned N, auto& Inst, auto M> struct Trampoline { static void isr(); }`, constrained to valid IRQ numbers and to members callable as `void()` on `Inst`, and `consteval VectorTable make_flash_table<Tr...>()` that rejects two trampolines on one IRQ.
>
> Test on the host with a fake NVIC (enable/pending bits, IPSR, VTOR): fire a deterministic xorshift32 sequence of IRQ numbers across ≥ 3 UART instances and check per-instance counts and checksums, and that unregistered IRQs reach the default handler — through both the RAM table and the flash table.
>
> Constraints: `-std=c++20 -fno-exceptions -fno-rtti`, no heap.
>
> Follow-ups: Why can't the vector table hold a member function pointer? Is putting a static member function into a C-linkage function-pointer table standard-conforming? What does a missing `extern "C"` do at link time? What does the trampoline cost on Cortex-M4? Why must a C++ vector table be `constinit`?

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
```

처음 실행 결과 (실제 출력, PASS 줄 생략, 가운데 중략):

```text
FAIL starter.cpp:312  reg.attach_raw(5, &rec_a, &g_tag_a) && reg.bound(5)
FAIL starter.cpp:314  reg.detach(5) && !reg.bound(5) && !reg.detach(5)
FAIL starter.cpp:318  g_registry.attach_raw(10, &rec_a, &g_tag_a)
FAIL starter.cpp:321  g_a_hits == 1 && g_mismatch == 0 && g_core.ipsr == 0
...
FAIL starter.cpp:416  !ValidIrqTrampoline<96>
FAIL starter.cpp:426  (has_duplicate_irq<T1, T2, T1dup>())
FAIL starter.cpp:428  kFlashTable[16 + 37] == &T1::isr && kFlashTable[16 + 52] == &T4::isr
FAIL starter.cpp:447  flash == exp_flash
FAIL starter.cpp:448  flash == ram
info: rx = 0 0 0 0, spurious = 0 0 0 0, unhandled IRQs = 0, entries = 20000
15/64 passed
```

- 하네스 설계 — **mock 코어** (`Core`): IRQ 96개의 enable/pending 비트, IPSR, VTOR(현재 벡터 테이블 포인터). `nvic_set_pending(n)` 은 enable 이고 다른 ISR 안이 아니면 **즉시** `(*vtor)[16+n]()` 을 부른다 — 모든 IRQ 같은 우선순위(중첩 없음), 진입 시 pending 해제. 0 인 엔트리로 점프하면 `null_vector` 를 센다 (실제 칩이면 HardFault).
- **틈**: `arm_irq(n)` 으로 장전하면 다음 `isr_window()` 에서 그 IRQ 가 올라온다. `store_binding()` 이 `fn` 과 `obj` 쓰기 사이에 그 틈을 갖고 있다 — thumbv7em 에서 실제로 나오는 `str.w; str.w` 두 명령의 모델이다 (5. 풀이).
- **레코더** `rec_a`/`rec_b`: 받은 `void*` 를 기대한 태그 주소와 **비교만** 한다. 찢어진 바인딩 `{rec_b, nullptr}` 도 UB 없이 관찰된다 (`g_mismatch`).
- 스타터의 스텁은 아무것도 등록하지 않고 `false` 를 돌려줘서, 가짜 IRQ 2만 번이 전부 "아무 일도 안 함" 으로 끝난다 (멈춤·크래시 없음). `trampoline_ok` 는 일부러 `true` 라서 `!ValidIrqTrampoline<96>` 이 FAIL 로 보인다. 컴파일 타임 증명 5개 (`static_assert`) 는 처음부터 성립한다.

관찰용 파일 (하네스와 별개, 결과는 5. 풀이에 실제 출력으로. 헤더는 CPP-A04 처럼 `ARM_INC` — 기본 `/tmp/arm-none-eabi-include`, 없으면 VM 에서 복사. `vm` 모드는 데모 3개를 VM `/tmp/CPP-A02/` 로 복사해서 링크 — 맥 홈은 VM 에서 읽기 전용):

```bash
bash asm_targets.sh asm       # demo_targets.cpp → thumbv7em asm: irq_trampoline, Trampoline<37>::isr, attach
bash asm_targets.sh sections  # .o 의 섹션·심볼·.isr_vector 재배치·멤버 함수 포인터 바이트
bash asm_targets.sh badsp     # reinterpret_cast 초기 SP / constinit / 동적 초기화 엔트리
bash asm_targets.sh linkage   # demo_linkage.cpp: 기본 빌드 + SHOW_ERROR_* 6가지 실제 에러
bash asm_targets.sh vm        # Lima VM: arm-none-eabi-g++ 14.2.1 링크 → 벡터 바이트, weak 덮어쓰기, extern "C" 누락
```

## 4. 힌트

<details>
<summary>힌트 1 — ISR 은 자기 IRQ 번호를 어떻게 아나</summary>

Cortex-M 은 예외 진입 때 IPSR 에 예외 번호를 넣는다. 외부 IRQ n 이면 16 + n. 그래서 모든 외부 벡터가 같은 함수를 가리켜도 그 함수는 `mrs r0, ipsr` 한 줄로 누가 불렀는지 안다 (모델에서는 `read_ipsr()`). 범위를 검사하는 건 IPSR 이 오염돼서가 아니라 "16 미만 = 시스템 예외" 가 같은 함수로 올 수 있기 때문이다 — 빼면 `unsigned` 빼기가 감싸서 거대한 인덱스가 된다.

</details>

<details>
<summary>힌트 2 — 두 워드 쓰기의 틈을 누가 볼 수 있나</summary>

단일 코어에서 main 의 두 `str` 사이를 볼 수 있는 건 **그 IRQ 의 ISR** 뿐이다. 그러면 그 IRQ 만 막으면 된다 (`NVIC->ICER`). 모든 인터럽트를 막는 PRIMASK 도 맞지만 다른 IRQ 의 지연까지 늘린다. 복원은 "켜기" 가 아니라 "원래대로" — 원래 꺼져 있던 IRQ 를 켜 버리면 안 된다 (CPP-B06 의 저장/복원 규칙과 같은 이유).

</details>

<details>
<summary>힌트 3 — enable 순간 무엇이 일어나나</summary>

pending 비트는 enable 과 무관하게 설 수 있다 (부트로더가 남긴 것, 주변장치가 먼저 플래그를 세운 것). enable 하는 **그 명령 직후** 에 ISR 이 들어온다. 그 순간 테이블에 주인이 있어야 한다. stop 은 역순: 먼저 끄면 그 뒤로는 pending 으로 쌓일 뿐 아무도 부르지 않는다.

</details>

<details>
<summary>힌트 4 — 컴파일 타임 테이블을 만드는 법</summary>

`VectorTable t{}` 를 만들고 루프로 `Default_Handler` 를 채운 뒤, 쉼표 fold `((t[16 + Tr::irq] = &Tr::isr), ...)` 로 덮어쓴다. `consteval` 함수 안이라 대입·루프 모두 된다. 중복 검사는 `{Tr::irq...}` 배열 이중 루프 — 빈 팩이면 크기 0 배열이 되므로 sentinel 하나를 덧붙인다.

</details>

## 5. 풀이

### 0. 언어 링크: 표준이 말하는 것 vs 컴파일러가 하는 것 — `demo_linkage.cpp`

벡터 테이블 엔트리의 정확한 타입은 "C 언어 링크를 가진 `void(void)` 함수의 포인터" 다. 관련 규칙 세 개 (N4861 원문 확인):

| 조항 | 내용 | 이 레슨에 주는 뜻 |
|---|---|---|
| [temp.pre]/6 | "A template, a template explicit specialization, and a class template partial specialization shall not have C linkage." | 템플릿 함수 하나로 `USART1_IRQHandler` 같은 C 이름을 만들 수 없다 |
| [dcl.link]/1 | "Two function types with different language linkages are distinct types even if they are otherwise identical." | `void()` (C++) 와 `extern "C" void()` 는 **다른 타입** — 그래서 한쪽 포인터를 다른 쪽 변수에 넣는 변환은 없다 |
| [dcl.link]/5 | C 언어 링크는 클래스 멤버 이름과 **멤버 함수의 함수 타입** 을 정할 때 무시된다. 또 linkage-specification 은 네임스페이스 범위에만 | 정적 멤버 함수(`Trampoline::isr`)는 항상 C++ 링크 타입이다 |

표준대로라면 `isr_t* const tbl[] = { &Trampoline<...>::isr };` 는 ill-formed 다. 실제로는 (`bash asm_targets.sh linkage`, 실제 출력):

```text
== 기본 빌드 (-Wall -Wextra -pedantic -Werror)
is_same_v<C-linkage fn, C++ fn> = 1, g_uart1.hits = 1
== -DSHOW_ERROR_TEMPLATE
demo_linkage.cpp:38:1: error: templates must have C++ linkage
== -DSHOW_ERROR_OVERLOAD
demo_linkage.cpp:43:6: error: redefinition of 'reg'
== -DSHOW_ERROR_LOCAL
demo_linkage.cpp:53:30: error: non-type template argument is not a constant expression
== -DSHOW_ERROR_TEMP
demo_linkage.cpp:61:7: error: call to deleted member function 'attach'
== -DSHOW_ERROR_RANGE
demo_linkage.cpp:57:5: error: constraints not satisfied for class template 'Trampoline' [with N = 200, Inst = g_uart1, M = &Uart::on_irq]
== -DSHOW_ERROR_ALIAS
demo_linkage.cpp:47:61: error: aliases are not supported on darwin
== -DSHOW_ERROR_ALIAS on thumbv7em (ELF 는 alias 지원)
demo_linkage.cpp:47:61: error: alias must point to a defined variable or function
demo_linkage.cpp:47:61: note: the function or variable specified in an alias must refer to its mangled name
demo_linkage.cpp:47:61: note: function by that name is mangled as "_Z11cpp_defaultv"
```

VM 의 `arm-none-eabi-g++ 14.2.1 -pedantic` 도 같은 판정이었다: 기본 빌드 통과 (경고 0), `error: template with C linkage`, `error: redefinition of 'void reg(void (*)())'`, `error: the address of 'local' is not a valid template argument because it does not have static storage duration`. 읽는 법:

- **Apple clang 21 은 두 타입을 같은 타입으로 본다** — `std::is_same_v` 가 1 이고, 언어 링크만 다른 두 오버로드는 "재정의" 에러다 (다른 타입이면 오버로드가 됐을 것). `-pedantic -Werror` 에서도 정적 멤버 함수를 C 링크 포인터 배열에 넣는 줄에 경고가 없다. GCC 14 도 같다. 즉 **이 레슨의 Part C 는 표준 기준으로는 비준수, 두 주류 컴파일러에서는 동작** 이다. 언어 링크로 타입을 구분하는 컴파일러가 있었다고 알려져 있지만(예: 옛 Sun/Oracle Studio) 이 레슨에서 확인하지는 않았다 — 미확인.
- **완전히 이식 가능한 형태** 는 IRQ 마다 `extern "C"` 비템플릿 함수 한 줄이다: `extern "C" void USART1_IRQHandler(void) { T1::isr(); }`. 같은 모양의 함수(VM `tc.cpp` 의 `direct()`)를 GCC 14 는 `ldr r0, .L6; b on_irq` 두 명령으로 냈다. 이름이 있는 C 스타트업(`startup_stm32xxx.s`)과 함께 쓸 때는 어차피 이 형태가 필요하다 — 스타트업이 **이름** 으로 참조하기 때문이다.
- **alias 는 맹글링된 이름을 가리켜야 한다.** `alias("cpp_default")` 는 clang/GCC 모두 거부한다. 그래서 기본 핸들러도 `extern "C"` 로 정의한다. Mach-O(맥) 는 alias 자체가 없다 — 호스트 하네스가 weak alias 대신 테이블을 `Default_Handler` 로 채우는 이유다.
- 참조 NTTP `auto& Inst` 는 정적 저장 기간 객체만 받는다 ([temp.arg.nontype]). 지역 객체 바인딩은 컴파일 에러 — 수명 버그 하나가 타입에서 사라진다.

### Part A — 등록 테이블과 트램펄린 하나

```cpp
using Thunk = void (*)(void*);
struct IrqBinding { Thunk fn = nullptr; void* obj = nullptr; };          // 2워드
template <class T, auto M> static void member_thunk(void* p) { std::invoke(M, *static_cast<T*>(p)); }   // CPP-M12 스텁

void dispatch(unsigned irq) const {
    if (irq < kNumIrq) {
        const IrqBinding b = slots_[irq];      // 한 번 복사 — fn 과 obj 를 같은 시점의 것으로
        if (b.fn != nullptr) { b.fn(b.obj); return; }
    }
    unhandled_irq(irq);                        // 기록 + nvic_disable(irq)
}
extern "C" void irq_trampoline(void) { g_registry.dispatch(read_ipsr() - 16u); }
```

설계 판단:

| 결정 | 이유 |
|---|---|
| 바인딩 = `{thunk, void*}` (CPP-M12 의 Delegate 와 같은 모양) | 타입 정보는 thunk 의 템플릿 인자에, 테이블은 타입 없는 2워드. 다른 클래스(Spi, Timer)도 같은 테이블에 |
| 트램펄린 **하나** + IPSR | 외부 IRQ 96개에 함수 96개를 만들 필요 없다. RAM 벡터 테이블의 외부 엔트리가 전부 같은 주소 |
| "IRQ 하나에 주인 하나" (`attach` 가 이미 주인 있으면 `false`) | 두 드라이버가 같은 IRQ 를 잡는 설정 실수를 조용히 덮어쓰지 않고 드러낸다 (공유 라인은 연습 2) |
| 미처리 IRQ → 기록 후 **그 IRQ disable** | 레벨 트리거 주변장치의 플래그를 아무도 안 지우면 같은 IRQ 가 끝없이 재진입해서 main 이 굶는다. 무한 루프 기본 핸들러(디버그용)와 달리 현장 펌웨어는 살아서 로그를 남긴다 |
| `std::invoke(M, obj)` | 멤버 함수 포인터 호출 문법(`(obj.*M)()`) 을 숨기고, 나중에 자유 함수·람다 바인딩과 같은 코드로 |
| 타입 있는 `attach<&Uart::on_irq>(irq, T&)` + `const T&&` 삭제 | 임시 객체 등록(문장 끝에서 소멸) 과 const 객체(비 const 핸들러) 를 컴파일 에러로 |

하네스의 컴파일 타임 증명 5개 (스타터에서도 성립): `AttachableUart<Uart&>` 참, `<Uart>` (임시 → deleted 오버로드) 와 `<const Uart&>` (const 객체에 비 const `on_irq`) 거짓, `IrqMember<&Uart::status, Uart>` (`uint32_t() const`) 거짓, `sizeof(void (Uart::*)()) == 2 * sizeof(void*)`. `AttachableUart<U>` 는 `requires(IrqRegistry& r, U&& u) { r.attach<&Uart::on_irq>(37u, std::forward<U>(u)); }` 다. 템플릿 밖에서 바로 `requires { r.attach(..., Uart{}); }` 를 쓰면 의존 표현식이 아니라서 거짓이 아니라 **하드 에러** 가 난다 — 그래서 개념(concept)의 매개변수로 감싼다.

Part A 테스트가 확인하는 것: 범위·null·중복 거부, IPSR 26 → IRQ 10 → `rec_a(&g_tag_a)` 이고 복귀 후 IPSR 0, 등록 안 된 IRQ 11 → 기록 1 + disable → 다시 올려도 pending 만 남고 기록은 그대로 1, `dispatch(200)` → `bad_irq`, 실제 `Uart` 등록 후 수신 1바이트와 데이터 없는 IRQ (드라이버의 `spurious`).

### Part B — 등록 경쟁: 두 워드 사이의 IRQ

등록은 2워드 쓰기다. 컴파일러가 실제로 무엇을 내는지 (`bash asm_targets.sh asm`, clang 21 thumbv7em, `demo_targets.cpp` 의 `attach_uart5`):

```text
_Z12attach_uart5v:
	movw	r0, :lower16:g_slots
	movw	r1, :lower16:_Z12member_thunkI4UartXadL_ZNS0_6on_irqEvEEEvPv
	movt	r0, :upper16:g_slots
	movt	r1, :upper16:_Z12member_thunkI4UartXadL_ZNS0_6on_irqEvEEEvPv
	str.w	r1, [r0, #424]        @ slots[53].fn
	movw	r1, :lower16:g_uart5
	movt	r1, :upper16:g_uart5
	str.w	r1, [r0, #428]        @ slots[53].obj
	bx	lr
```

(`@ slots[53]...` 주석은 읽기 쉽게 붙인 것이다.) `fn` 이 먼저, `obj` 가 3명령 뒤다. 그 사이에 IRQ 53 이 오면 ISR 은 `{member_thunk, nullptr}` 을 읽고 `nullptr->on_irq()` — HardFault 다. 그리고 이 순서는 **약속이 아니다**: 두 일반 store 의 순서는 컴파일러가 바꿀 수 있다 (`obj` 먼저면 `{옛 fn, 새 obj}` — 다른 클래스의 thunk 에 이 객체가 들어가는 타입 혼동). 하네스의 `store_binding()` 은 실측 순서 그대로 `fn; isr_window(); obj` 다.

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">enable 된 IRQ 13 에 attach — 두 워드 사이에 IRQ 13 이 온다</text>
  <text x="10" y="62" fill="currentColor">보호 없음</text>
  <rect x="110" y="44" width="120" height="30" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="118" y="64" fill="currentColor" font-family="monospace">str fn=rec_b</text>
  <rect x="240" y="44" width="230" height="30" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="248" y="64" fill="currentColor" font-family="monospace">ISR: rec_b(nullptr) 찢어짐</text>
  <rect x="480" y="44" width="130" height="30" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="488" y="64" fill="currentColor" font-family="monospace">str obj=&amp;tb</text>
  <text x="620" y="64" fill="currentColor">mismatch 1</text>
  <text x="10" y="132" fill="currentColor">IrqMaskGuard</text>
  <rect x="110" y="114" width="110" height="30" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="118" y="134" fill="currentColor" font-family="monospace">ICER: 13 끔</text>
  <rect x="225" y="114" width="115" height="30" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="232" y="134" fill="currentColor" font-family="monospace">str fn=rec_b</text>
  <rect x="345" y="114" width="120" height="30" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="352" y="134" fill="currentColor" font-family="monospace">str obj=&amp;tb</text>
  <rect x="470" y="114" width="115" height="30" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="477" y="134" fill="currentColor" font-family="monospace">ISER: 복원</text>
  <rect x="590" y="114" width="140" height="30" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="597" y="134" fill="currentColor" font-family="monospace">ISR: rec_b(&amp;tb)</text>
  <path d="M342 160 L342 148" stroke="currentColor" stroke-width="2"/>
  <text x="230" y="176" fill="currentColor">IRQ 13 요청 → disable 이라 pending</text>
  <path d="M590 176 Q 560 190 540 148" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="470" y="198" fill="currentColor">복원 순간 진입 (IRQ 유실 없음)</text>
  <text x="10" y="232" fill="currentColor" font-size="12">다른 IRQ 는 그대로 실행된다 (PRIMASK 와의 차이).</text>
  <text x="10" y="252" fill="currentColor" font-size="12">실제 칩: ICER 쓰기 뒤 DSB+ISB 로 "이후로 안 들어옴" 을 보장 (CMSIS NVIC_DisableIRQ).</text>
</svg>
```

```cpp
class IrqMaskGuard {
public:
    explicit IrqMaskGuard(unsigned irq) : irq_(irq), was_enabled_(nvic_is_enabled(irq)) { nvic_disable(irq_); }
    ~IrqMaskGuard() { if (was_enabled_) nvic_enable(irq_); }     // pending 이었으면 여기서 진입 — 새 바인딩으로
    ...
};
bool attach_raw(unsigned irq, Thunk fn, void* obj) {
    if (irq >= kNumIrq || fn == nullptr || slots_[irq].fn != nullptr) return false;
    IrqMaskGuard g(irq);
    store_binding(slots_[irq], fn, obj);
    return true;
}
```

세 가지 해결책 비교:

| 방법 | 막는 범위 | 비용 | 언제 |
|---|---|---|---|
| `IrqMaskGuard` (그 IRQ 만 ICER/ISER) | 그 IRQ 의 ISR | NVIC 레지스터 쓰기 2번 + DSB/ISB, 다른 IRQ 지연 0 | 이 레슨의 기본. 단일 코어, 바인딩을 읽는 곳이 그 ISR 뿐일 때 |
| PRIMASK 임계 구역 (CPP-B06) | 모든 IRQ | 전체 인터럽트 지연이 구역 길이만큼 | 바인딩을 다른 ISR 도 읽거나, IRQ 번호를 모를 때 |
| 한 워드 게시: 슬롯 = `const IrqBinding*`, 바인딩은 `constexpr` (플래시) | 찢어짐 자체가 없음 | 디스패치에 로드 1개 추가, 바인딩 객체마다 8 B 플래시 | 실행 중 교체(rebind) 가 잦을 때, 다중 코어 (연습 1) |

멀티코어 (STM32H7 의 M7+M4, RP2040) 에서는 다른 코어의 ISR 이 같은 테이블을 볼 수 있다 — NVIC 마스크는 **자기 코어** 것이라 막지 못한다. 그때는 한 워드 게시 + `std::atomic` release/acquire 가 답이다 (CPP-A04 의 lock-free 정책).

뮤턴트로 확인: `attach_raw` 에서 가드를 빼면 `g_mismatch == 0` 이 FAIL 한다 (ISR 이 `rec_b(nullptr)` 를 봄) — 그 다음 두 CHECK 도 같이 실패해 59/64.

### Part B — 등록 → enable 순서와 수명

`start()` = `attach<&Uart::on_irq>(irq_, *this)` → `nvic_enable(irq_)`, `stop()` = `nvic_disable` → `detach` (역순), `~Uart()` 가 `stop()`. 두 번 `start` 는 `false`.

| 시나리오 (테스트) | 올바른 순서 | 틀린 순서 |
|---|---|---|
| enable 전에 이미 pending (부트로더가 남긴 RXNE) | enable 순간 `on_irq` 로, 바이트 0x5A 수신 | enable → 주인 없음 → 기본 핸들러 → **IRQ 꺼짐** → 그 뒤 등록해도 UART 는 조용히 죽어 있다 (`rx_count == 0`, 실측 테스트) |
| `stop()` 도중 IRQ | 먼저 꺼서 pending 으로만 남음 → 재시작하면 새 주인에게 (`spurious == 1`) | detach 가 먼저면 그 IRQ 가 기본 핸들러로 가서 기록 1 + disable (뮤턴트: 3 FAIL) |
| 드라이버 객체가 스코프를 벗어남 | `~Uart → stop()`: 바인딩 없음, disable. 나중에 누가 다시 켜도 기본 핸들러로 (해제된 객체를 안 건드림 — ASan 깨끗) | 바인딩이 남으면 다음 IRQ 가 죽은 스택 객체의 `on_irq` 를 부른다 (use-after-scope) |

"틀린 순서" 는 링크도 되고 대부분의 테스트도 통과한다 — pending 이 없는 보드에서는 증상이 없다가 부트로더를 바꾼 날 첫 바이트가 사라지는 종류의 버그다. 그래서 테스트가 pending 을 일부러 미리 세운다.

전역 드라이버 객체의 소멸자는 펌웨어가 `main` 에서 돌아오지 않으면 실행되지 않는다. 그래도 `~Uart` 가 사소하지 않으면 전역 소멸자 등록 코드(`__aeabi_atexit` 등)가 링크될 수 있다 — 무엇이 끌려오고 어떻게 없애는지는 CPP-A03 이 실측한다.

### Part B — 컴파일 타임 바인딩 `Trampoline<N, Inst, M>`

```cpp
template <unsigned N, auto& Inst, auto M>
    requires (trampoline_ok(N) && IrqMember<M, std::remove_reference_t<decltype(Inst)>>)
struct Trampoline {
    static constexpr unsigned irq = N;
    static void isr() { std::invoke(M, Inst); }
};
using T1 = Trampoline<USART1_IRQn, g_u1, &Uart::on_irq>;   // 37 (STM32F4 대표값)
```

- 객체 주소와 멤버가 **템플릿 인자** 라서 `isr` 안에는 런타임 데이터가 없다 — 주소는 즉시값(movw/movt) 이나 리터럴 풀 한 워드다.
- `requires` 절은 SFINAE 친화적이라 `ValidIrqTrampoline<96>` 같은 개념으로 "컴파일 안 됨" 을 테스트할 수 있다. 클래스 안 `static_assert` 였다면 개념은 참이 되고 인스턴스화할 때 하드 에러가 났을 것이다.
- 비용은 유연성: 인스턴스도 IRQ 도 빌드 때 정해진다. 보드 변형마다 UART 수가 다르거나, 부트로더/앱이 같은 IRQ 를 다르게 쓰거나, 실행 중 드라이버를 바꾸는 경우엔 Part A 가 필요하다.

### 실측 — 두 핸들러의 asm

clang 21 (`bash asm_targets.sh asm`, `-O2`, thumbv7em, `on_irq` 는 noinline 으로 분리):

```text
irq_trampoline:
	@APP
	mrs	r0, ipsr
	@NO_APP
	subs	r0, #16
	cmp	r0, #95
	bhi	.LBB1_3
	movw	r2, :lower16:g_slots
	movt	r2, :upper16:g_slots
	ldr.w	r1, [r2, r0, lsl #3]
	cbz	r1, .LBB1_3
	add.w	r0, r2, r0, lsl #3
	ldr	r0, [r0, #4]
	bx	r1
.LBB1_3:
	movw	r0, :lower16:g_unhandled
	...                         @ 기본 핸들러가 인라인됨 (ldr/adds/str/bx)
_Z12member_thunkI4UartXadL_ZNS0_6on_irqEvEEEvPv:
	b	_ZN4Uart6on_irqEv
_ZN10TrampolineILj37EL_Z7g_uart1EXadL_ZN4Uart6on_irqEvEEE3isrEv:
	movw	r0, :lower16:g_uart1
	movt	r0, :upper16:g_uart1
	b	_ZN4Uart6on_irqEv
```

arm-none-eabi-g++ 14.2.1 (`bash asm_targets.sh vm`, 링크된 ELF):

```text
080001d0 <irq_trampoline>:            (clang 과 같은 모양: mrs/subs/cmp/bhi, 그다음)
 80001da:	ldr	r2, [pc, #24]	@ (80001f4 <irq_trampoline+0x24>)   ← g_slots 주소를 리터럴 풀에서
 ...                                 (ldr.w fn / cbz / add.w / ldr obj / bx r1)
08000258 <Trampoline<37u, g_uart1, &Uart::on_irq>::isr()>:
 8000258:	push	{r3, lr}
 800025a:	ldr	r0, [pc, #8]	@ (8000264 <Trampoline<37u, g_uart1, &Uart::on_irq>::isr()+0xc>)
 800025c:	bl	8000214 <Uart::on_irq()>
 8000260:	pop	{r3, pc}
 8000262:	nop
 8000264:	.word	0x20000024
```

| on_irq 까지 (정상 경로) | clang 21 | GCC 14.2 | 데이터 로드 | 분기 |
|---|---|---|---|---|
| (1) `irq_trampoline` → thunk | 11 + 1 = **12 명령**, 리터럴 풀 없음 (movw/movt) | 10 + 1 명령, 리터럴 풀 1워드 (`ldr r2,[pc]`) | fn, obj 2개 (+ GCC 는 풀) | `bx r1` 간접 1 + `b` 직접 1 |
| (2) `Trampoline<37>::isr` | **3 명령**, 12 B | `push/ldr/bl/pop` 4 명령 + 풀 1워드 | 0 (+ GCC 는 풀) | `b` 직접 1 (GCC: `bl`) |

솔직하게 짚을 것:

- GCC 의 `Trampoline::isr` 가 꼬리 호출(`b`) 대신 `push/bl/pop` 인 것은 이 데모에서 `on_irq` 가 **같은 TU 에 `[[gnu::noinline]]` 으로 정의** 됐을 때다. `on_irq` 를 선언만 하고 다른 TU 에 둔 소스(`tc.cpp`, VM 에서 따로 확인)에서는 GCC 도 `ldr r0, .L3; b _ZN4Uart6on_irqEv` 두 명령이었다. 왜 noinline 정의일 때 꼬리 호출을 안 하는지는 미확인.
- 실제로는 `on_irq` 가 인라인 가능하면 (2) 는 핸들러 본체가 `isr` 안으로 들어와 분기 0 이 된다. (1) 은 간접 호출이라 컴파일러가 대상을 모른다 — 인라인 불가.
- 사이클은 재지 않았다 (맥에 DWT 없음). 위는 명령 수다. Cortex-M4 의 예외 진입(스태킹)은 무대기 메모리에서 12 사이클로 ARM 자료에 나와 있어서 (미측정) 수 명령의 차이는 대개 진입 비용보다 작다 — 문제는 지연보다 **간접 호출이라 인라인·정적 분석이 막히는 것** 과 RAM 테이블의 크기다.

### Part C — 벡터 테이블: 플래시 vs RAM — `demo_targets.cpp`

C++ 로 정의한 벡터 테이블 (`consteval` 로 계산해 `constinit const` 로 고정):

```cpp
union Vec { const void* sp; isr_t* fn; };          // [0] 만 데이터 주소 — 캐스트 없이 상수식
struct VecTable { Vec v[16 + kNumIrq]; };
consteval VecTable make_vectors() { ... t.v[0].sp = &_estack; t.v[16 + 37].fn = &U1::isr; ... }
__attribute__((section(".isr_vector"), used)) constinit const VecTable g_vectors = make_vectors();
```

`bash asm_targets.sh sections` (clang .o, 실제 출력 발췌):

```text
 30 .data            00000040 00000000 DATA
 32 .bss             000005c0 00000000 BSS
 33 .isr_vector      000001c0 00000000 DATA
--- symbols
00000010 g     F .text	00000030 irq_trampoline
00000000 g     F .text	00000010 Default_Handler
00000000  w    F .text	00000010 HardFault_Handler
00000000  w    F .text	00000010 UART4_IRQHandler
00000000  w    F .text._ZN10TrampolineILj37EL_Z7g_uart1EXadL_ZN4Uart6on_irqEvEEE3isrEv	0000000c ...
00000000 l     O .isr_vector	000001c0 _ZL9g_vectors
00000400 g     O .bss	000001c0 g_ram_vectors
00000000 g     O .bss	00000300 g_slots
--- (요약) .isr_vector 재배치: _estack, Reset_Handler, Default_Handler, HardFault_Handler, ... SysTick_Handler (예약 슬롯 7–10, 13 은 재배치 없음)
```

| 항목 | 위치 | 크기 (실측) | 비고 |
|---|---|---|---|
| 플래시 벡터 테이블 `g_vectors` (16+96 엔트리) | `.isr_vector` | 0x1c0 = 448 B | 재배치만 있고 초기화 코드 없음. 예약 슬롯 7–10, 13 은 재배치 없이 0 |
| RAM 벡터 테이블 `g_ram_vectors` | `.bss` | 448 B, **오프셋 0x400** | `alignas(512)`: VTOR 는 엔트리 수×4 이상의 2의 거듭제곱 정렬 (BM-A01). `.bss` 0x5c0 중 일부는 정렬 패딩 |
| 등록 테이블 `g_slots` (96 × 8 B) | `.bss` | 0x300 = 768 B | 런타임 경로의 실제 RAM 비용 — 벡터 테이블보다 크다 |
| `irq_trampoline` (기본 경로 인라인 포함) | `.text` | 48 B | 하나뿐 |
| `Trampoline<N>::isr` | `.text.<mangled>` (weak, COMDAT) | 12 B 씩 | IRQ 마다 하나 |
| weak alias `HardFault_Handler`, `UART4_IRQHandler` | `.text` | `Default_Handler` 와 **같은 주소** | 강한 정의가 오면 링커가 그쪽을 택한다 |

VM 링크 (`vm_link.ld`, `--gc-sections`, `bash asm_targets.sh vm`):

```text
=== weak.elf
   text	   data	    bss	    dec	    hex	filename
    624	     48	    772	   1444	    5a4	weak.elf
080001c0 T Default_Handler
080001c0 W UART4_IRQHandler
080001d0 T irq_trampoline
08000258 W _ZN10TrampolineILj37EL_Z7g_uart1EXadL_ZN4Uart6on_irqEvEEE3isrEv
20020000 B _estack
 8000000 00000220 fd010008 c1010008 c1010008  ... ............
 80000d0 c1010008 59020008 49020008 39020008  ....Y...I...9...
 8000110 c1010008 d1010008 c1010008 c1010008  ................
=== strong.elf   (demo_strong_uart4.cpp 의 extern "C" UART4_IRQHandler 추가)
08000268 T UART4_IRQHandler
 8000110 69020008 d1010008 c1010008 c1010008  i...............
=== forgot: demo_strong_uart4.o 가 실제로 정의한 심볼   (-DFORGET_EXTERN_C)
00000000 T _Z16UART4_IRQHandlerv
=== forgot.elf
    624	     48	    772	   1444	    5a4	forgot.elf
080001c0 W UART4_IRQHandler
 8000110 c1010008 d1010008 c1010008 c1010008  ................
```

읽는 법 (워드는 리틀엔디언):

- `[0] = 0x20020000` (`_estack`), `[1] = 0x080001fd` (Reset_Handler + 1). 함수 주소가 전부 **홀수** — Thumb 비트. 손으로 주소를 계산해 넣는 C 테이블에서 `+1` 을 빼먹으면 첫 IRQ 에서 UsageFault (INVSTATE) 다. `&함수` 를 쓰면 링커가 붙여 준다.
- 행 0xd0 = 벡터 52..55: `[53] = 0x08000259` (`T1::isr`), `[54]`, `[55]` = U2, U3 — 정적 멤버 함수가 C 링크 슬롯에 그대로 들어갔다. 행 0x110 의 `[69] = 0x080001d1` 은 IRQ 53 → `irq_trampoline` (런타임 경로).
- `[68]` (IRQ 52, UART4): weak 이면 `0x080001c1` = Default_Handler, 다른 TU 에 `extern "C"` 강한 정의가 있으면 `0x08000269`. **`extern "C"` 를 빼먹으면** 오브젝트에는 `_Z16UART4_IRQHandlerv` 가 정의되고, 테이블은 여전히 Default 를 가리키며, 링크는 경고 없이 성공하고, `--gc-sections` 가 아무도 참조하지 않는 내 핸들러를 지운다 (`forgot.elf` 크기 = `weak.elf` 와 동일 624). 현장에서 "인터럽트가 안 들어와요" 의 1순위 원인.
- `gc-sections` 후 `.bss` 772 = `g_slots` 768 + `g_unhandled` 4. 아무도 안 쓰는 `g_ram_vectors` 는 제거됐다. 플래시 테이블은 `KEEP(*(.isr_vector))` + `used` 로 살아남는다 — 아무 코드도 참조하지 않는 데이터라 KEEP 이 없으면 통째로 사라진다.

### Part C — 왜 `constinit` 인가 — `bash asm_targets.sh badsp`

C 스타트업은 `(void(*)(void))&_estack` 같은 캐스트로 SP 를 넣는다. C++ 로 옮기면 그 `reinterpret_cast` 는 상수식이 아니다 ([expr.const]) — 그러면 테이블은 **동적 초기화** 대상이 되어, 리셋 직후 코어가 읽는 순간 아직 0 일 수 있다. 실제로 확인했다 (실제 출력):

```text
== -DBAD_SP_CAST (reinterpret_cast, constinit 없음)
 18 .isr_vector                                                         000001c0 00000000 DATA
 19 .rel.isr_vector                                                     00000010 00000000
00000000 R_ARM_ABS32              _estack
00000004 R_ARM_ABS32              Reset_Handler
== -DBAD_SP_CAST -DWITH_CONSTINIT
demo_targets.cpp:95:20: error: variable does not have a constant initializer
demo_targets.cpp:91:47: note: required by 'constinit' specifier here
demo_targets.cpp:96:5: note: reinterpret_cast is not allowed in a constant expression
== -DBAD_SP_CAST -DDYN_ENTRY (엔트리 하나가 함수 호출 결과)
 18 .isr_vector                                                         000001c0 00000000 DATA
 19 .init_array                                                         00000004 00000000 DATA
 0000 00000000 00000000 00000000 00000000  ................
--- .rel.isr_vector:
--- _GLOBAL__sub_I 가 부르는/쓰는 것:
0000008a R_ARM_THM_MOVW_ABS_NC    g_vectors
0000009a R_ARM_THM_CALL           __aeabi_memclr4
0000009e R_ARM_THM_MOVW_ABS_NC    _estack
000000a8 R_ARM_THM_MOVW_ABS_NC    Reset_Handler
000000b2 R_ARM_THM_CALL           _Z14pick_hardfaultv
```

결과는 예상과 달랐다: **캐스트만 있는 판은 clang 도 GCC 14 도 (-O0 포함) 정적으로 초기화했다** — 재배치 2개, `.init_array` 없음. [basic.start.static]/3 이 구현에게 "동적 초기화 대신 정적 초기화를 해도 된다" 고 허용하기 때문이다. 하지만 **허용이지 보장이 아니다.** `constinit` 을 붙이면 그 의존이 컴파일 에러로 드러나고, 엔트리 하나라도 진짜 런타임 값(`pick_hardfault()`)이면 테이블 전체가 0 으로 남고 (`.rel.isr_vector` 비어 있음) `_GLOBAL__sub_I` 생성자가 `__aeabi_memclr4` 후 SP·Reset 을 써 넣는다 — 그 생성자를 부를 Reset_Handler 의 주소가 바로 그 0 인 테이블에 있다. ARMv7-M 규칙대로라면 리셋 벡터의 Thumb 비트가 0 이라 첫 명령에서 fault, 그 fault 의 핸들러 주소도 0 이라 코어는 lockup 상태가 된다 (실기에서 재현하지는 않음). 그래서 규칙: **벡터 테이블은 `constinit`** (또는 `constexpr`), SP 는 union/구조체 멤버로 캐스트 없이.

멤버 함수 포인터를 엔트리로 쓸 수 없는 이유도 같은 `.o` 에서 보인다 (`sections` 모드):

```text
--- 멤버 함수 포인터 g_pmf_nv(.data+0x30) / g_pmf_v(.data+0x38)
 0030 00000000 00000000 00000000 01000000  ................
00000030 R_ARM_ABS32              _ZN3Dev2nvEv
```

비가상 `&Dev::nv` = `{함수 주소, 0}`, 가상 `&Dev::v` = `{0 (vtable 오프셋), 1}` — 8 B 이고 `this` 보정까지 들어 있다. ARM C++ ABI 는 함수 주소의 최하위 비트를 Thumb 비트로 쓰기 때문에 "가상 여부" 표시를 `adj` 의 최하위 비트로 옮겼다 (`adj = 2×조정 + 가상?`). 같은 소스를 두 번째 가상 함수로 바꿔 세 타깃에서 본 값: thumbv7em `{4, 1}`, arm64-apple `{8, 1}`, x86_64-apple (Itanium 일반형) `{9, 0}` = `1 + vtable 오프셋 8` (실측). 1워드 벡터 엔트리에 들어갈 수 없고, 호출에 객체도 필요하다.

### 하네스 시나리오 결과 (Part C)

solution 의 마지막 정보 줄 (실제 출력):

```text
info: rx = 3744 3723 3781 3762, spurious = 49 49 52 59, unhandled IRQs = 92, entries = 15311
```

2만 번 중 15010 번이 UART 수신 (네 인스턴스 합), 209 번이 데이터 없는 UART IRQ (드라이버의 `spurious`), 나머지 1/4 랜덤 번호 중 등록 안 된 92개 IRQ 가 **각각 처음 한 번만** 기본 핸들러에 기록되고 꺼졌다 — 15010 + 209 + 92 = 15311 = 실제 예외 진입 수. RAM 테이블(등록 + `irq_trampoline`) 과 플래시 테이블(`Trampoline::isr`) 의 `RunStats` 가 바이트 단위로 같고, 하드웨어 모델과 독립적으로 계산한 기대값과도 같다.

### 정리: 무엇을 언제

| 상황 | 선택 | 이유 |
|---|---|---|
| 인스턴스·IRQ 가 보드마다 고정, 성능·정적 분석 중요 | 플래시 테이블 + `Trampoline` (또는 IRQ 별 `extern "C"` 한 줄) | 테이블 조회 0, RAM 0, 링크 시점에 전부 보임 |
| 벤더 스타트업 `.s` 를 그대로 씀 | IRQ 별 `extern "C" void XXX_IRQHandler(void) { T::isr(); }` | 스타트업이 이름으로 참조. `extern "C"` 필수 |
| 드라이버가 실행 중 결정 (보드 변형 탐지, 부트로더 → 앱, 플러그인) | RAM 등록 테이블 + 트램펄린 하나 | 유연성. 대가: 768 B RAM, 간접 호출, 등록 경쟁·순서·수명 관리 |
| 실행 중 교체가 잦거나 다중 코어 | 한 워드 게시 (`atomic<const IrqBinding*>`) | 찢어짐 없음 (연습 1) |
| 한 IRQ 를 여러 소스가 공유 (EXTI9_5) | 공유 라인 리스트 (연습 2) | "IRQ 하나에 주인 하나" 의 예외 |
| ISR 을 짧게, 처리는 main 에서 | 지연 디스패치 비트맵 (연습 4) | ISR 은 비트 하나, 처리 순서는 main 이 결정 |

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution     # 64/64 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer solution.cpp -o sol_asan && ./sol_asan   # 64/64, 보고 없음
```

두 빌드 모두 마지막 줄이 `64/64 passed` 였다. ASan 이 의미 있는 곳은 "스코프를 벗어난 `Uart` 의 IRQ 를 나중에 다시 켜는" 테스트다 — 바인딩이 남아 있었다면 stack-use-after-scope 로 잡혔을 것이다.

하네스가 실제로 버그를 잡는지 뮤턴트로 확인했다 (solution 의 사본에 주입, 레슨 폴더에는 남기지 않음):

| 주입한 버그 | 결과 |
|---|---|
| `attach_raw` 에서 `IrqMaskGuard` 제거 | 59/64 — `g_mismatch == 0` 등 경쟁 테스트 FAIL |
| `start()` 에서 enable 을 등록보다 먼저 | 59/64 — 남은 pending 이 기본 핸들러로, UART 사망 |
| `stop()` 에서 detach 를 disable 보다 먼저 | 61/64 — stop 도중 IRQ 가 기본 핸들러로 |
| `~IrqMaskGuard` 가 무조건 enable | 58/64 — 원래 꺼져 있던 IRQ 가 켜짐 |
| 트램펄린에서 `- 16` 누락 | 42/64 |
| `has_duplicate_irq` 내부 루프 off-by-one | 61/64 — `T1dup` 미검출 |
| `make_flash_table` 의 Default 채우기 누락 | 59/64 — null 벡터 진입 (`null_vector` 증가) |

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| C++ 파일에 `void USART1_IRQHandler(void) {...}` | 맹글링(`_Z17USART1_IRQHandlerv`) → 스타트업의 weak 기본 핸들러가 그대로, 링크 에러 없음, gc 가 내 함수 삭제 (`forgot.elf` 실측) | `extern "C" void USART1_IRQHandler(void)` |
| 템플릿 함수로 핸들러 이름 만들기 시도 | [temp.pre]/6 — `error: templates must have C++ linkage` | 템플릿 클래스의 정적 멤버 + IRQ 별 `extern "C"` 한 줄, 또는 C++ 로 정의한 테이블 |
| 바인딩 2워드를 IRQ 가 켜진 채 그냥 대입 | 틈에서 `{새 fn, 옛 obj}` 또는 그 반대 — null 역참조나 타입 혼동. store 순서도 보장 없음 | `IrqMaskGuard` / PRIMASK / 한 워드 게시 |
| `NVIC_EnableIRQ` 후 콜백 등록 | 남은 pending 이 주인 없는 상태로 진입 | 등록 → enable, 해제는 disable → 해제 |
| 기본 핸들러 = `while(1);` 를 현장 빌드에 | 등록 안 된 IRQ 하나로 시스템 정지 (워치독 리셋 루프) | IPSR 기록 + 그 IRQ disable (디버그 빌드만 `bkpt`) |
| 기본 핸들러가 기록만 하고 IRQ 를 안 끔 | 레벨 트리거 소스면 같은 IRQ 무한 재진입, main 기아 | `unhandled_irq`: 기록 + `nvic_disable(irq)` |
| `IrqMaskGuard` 소멸자에서 무조건 enable | 원래 꺼져 있던 IRQ 를 켠다 | 진입 시 상태 저장, 그대로 복원 |
| 드라이버 객체를 지역 변수로 만들고 등록, 소멸자에서 해제 안 함 | 스코프 밖에서 IRQ → 죽은 객체 호출 | 소멸자에서 `stop()` (RAII), 또는 정적 객체만 (`Trampoline` 은 타입으로 강제) |
| 벡터 테이블 초기값에 `reinterpret_cast` 나 함수 호출 | 상수 초기화가 보장되지 않음 → 리셋 순간 0 일 수 있음 (`DYN_ENTRY` 실측: `.init_array` + `__aeabi_memclr4`) | `constinit const` + union 으로 SP, 엔트리는 `&함수` 만 |
| `.isr_vector` 에 `used`/`KEEP` 없음 | 아무 코드도 참조 안 하는 데이터 → `--gc-sections` 가 삭제 | `__attribute__((used))` + 링커 스크립트 `KEEP(*(.isr_vector))` |
| RAM 벡터 테이블을 `alignas` 없이 | VTOR 하위 비트가 무시되어 엉뚱한 주소에서 벡터를 읽음 | 엔트리 수×4 이상의 2의 거듭제곱 정렬 (112 엔트리 → 512) (BM-A01) |

## 8. 면접 Q&A

<details>
<summary>Q1. 멤버 함수를 벡터 테이블에 바로 넣으면 안 되나요?</summary>

두 가지 이유로 안 된다. ① 하드웨어는 인자 없이 점프한다 — `this` 를 줄 방법이 없다 (예외 진입은 r0–r3 를 스택에 저장할 뿐 값을 정하지 않는다). ② 멤버 함수 포인터는 1워드가 아니다. thumbv7em 에서 `void (Dev::*)()` 는 8 B 이고, 비가상은 `{주소, this 보정×2}`, 가상은 `{vtable 오프셋, 2×보정 + 1}` 이었다 (실측 `.data`). ARM C++ ABI 가 가상 표시를 `adj` 쪽으로 옮긴 건 주소의 최하위 비트가 Thumb 비트라서다 — x86_64 Itanium 은 `{1 + 오프셋, 0}` 이었다. 그래서 "객체를 아는 1워드 함수" 를 만들어야 하고, 그게 트램펄린이다: 런타임이면 `{thunk, obj}` 테이블 + 공통 진입점, 컴파일 타임이면 객체 주소를 템플릿 인자로 박은 정적 멤버 함수. 꼬리: "Cortex-M 핸들러에 `__attribute__((interrupt))` 가 필요한가?" → 아니다. 하드웨어가 AAPCS 의 caller-saved 레지스터를 스택에 저장하고 `EXC_RETURN` 을 LR 에 넣으니 보통 C 함수가 그대로 핸들러가 된다 (ARMv7-A/R 과 다른 점).

</details>

<details>
<summary>Q2. 정적 멤버 함수 주소를 C 함수 포인터 테이블에 넣는 건 표준적으로 맞나요?</summary>

엄밀히는 아니다. [dcl.link]/1: 언어 링크가 다른 함수 타입은 다른 타입이고, [dcl.link]/5: 멤버 함수의 함수 타입은 C 링크를 가질 수 없다. 그러니 `&T::isr` 는 `extern "C" void(*)()` 로 변환되지 않는다. 그런데 Apple clang 21 과 GCC 14 는 언어 링크를 타입에 반영하지 않는다 — `is_same_v` 가 true, 링크만 다른 오버로드는 "redefinition" 에러, `-pedantic -Werror` 에서도 경고 없음 (실측). 그래서 실무에서는 다들 쓰고 동작한다. 이식성이 필요하면 IRQ 마다 `extern "C"` 비템플릿 함수 한 줄 (`{ T1::isr(); }`) 로 감싸면 된다 — `b` 한 명령이라 비용이 거의 없다. 템플릿으로 그 한 줄을 생성할 수는 없다 ([temp.pre]/6, `error: templates must have C++ linkage`). 매크로가 그 자리를 채우는 이유다. 꼬리: "C 링크 포인터를 C++ 로 호출할 때 호출 규약이 다를 수 있나?" → 표준이 타입을 나눈 이유가 그것이지만 ARM EABI 에서는 C 와 C++ 의 호출 규약이 같은 AAPCS 다.

</details>

<details>
<summary>Q3. extern "C" 를 빼먹으면 어떻게 되나요? 링크 에러가 나나요?</summary>

안 난다 — 그게 위험하다. 벤더 스타트업은 `USART1_IRQHandler` 를 weak 로 `Default_Handler` 에 alias 해 두고, C++ 파일의 정의는 `_Z17USART1_IRQHandlerv` 같은 다른 이름이 된다. 링커는 weak 정의를 쓰고 끝난다. `--gc-sections` 면 내 함수는 아무도 참조하지 않으니 삭제된다. VM 실측: `forgot.elf` 의 `UART4_IRQHandler` 는 `W 080001c0` (= Default_Handler), 벡터 [68] 은 `0x080001c1`, ELF 크기는 weak 판과 똑같이 624 B. 증상은 "인터럽트가 안 들어온다" 또는 "Default_Handler 무한 루프에서 멈춤". 잡는 법: 빌드 후 `nm` 으로 핸들러 심볼이 `T` 인지 확인하는 스크립트, 또는 C++ 쪽에서 테이블을 정의해 이름 의존을 없애기. 꼬리: "alias 대상도 C++ 함수면?" → `alias must point to a defined variable or function ... mangled as "_Z11cpp_defaultv"` (clang 실측). alias 는 심볼 이름 수준이다.

</details>

<details>
<summary>Q4. 런타임 등록 테이블과 컴파일 타임 Trampoline 중 무엇을 고르나요? 비용은?</summary>

실측 (clang 21, thumbv7em): 런타임 경로는 `irq_trampoline` 11 명령 (`mrs ipsr`, 범위 검사, `ldr fn`, `cbz`, `ldr obj`, `bx`) + thunk 의 `b` 1 = 12 명령, 간접 분기 1, 데이터 로드 2, 그리고 `.bss` 768 B (96×8). 컴파일 타임은 `movw/movt r0, &g_uart1; b on_irq` 3 명령, 로드 0, RAM 0, 대상이 보이면 인라인까지 된다. GCC 14 는 리터럴 풀을 쓰는 차이만 있었다. 진입 스태킹이 수십 사이클 단위라 명령 몇 개 차이보다 중요한 건 ① RAM ② 간접 호출이라 정적 분석(스택 깊이, 호출 그래프)이 끊기는 것 ③ 등록 경쟁·순서·수명이라는 **새로운 버그 종류** 다. 기본은 컴파일 타임, 실행 중 결정이 필요한 곳(보드 변형, 부트로더/앱 인계, 교체 가능한 드라이버)만 등록. 꼬리: "둘을 섞을 수 있나?" → 이 레슨의 데모가 그렇다: 대부분 `Trampoline`, IRQ 53 하나만 `irq_trampoline`.

</details>

<details>
<summary>Q5. 등록하는 도중에 그 IRQ 가 들어오면? 어떻게 막나요?</summary>

바인딩은 2워드이고 thumbv7em 에서 `str.w fn; ...; str.w obj` 두 명령이었다 (실측). 사이에 그 ISR 이 오면 `{새 fn, 옛 obj}` — 빈 슬롯이었다면 `nullptr` 역참조, 교체였다면 다른 타입 객체로 thunk 호출. 게다가 두 일반 store 의 순서는 보장이 없다. 막는 법 세 가지: ① 그 IRQ 만 NVIC 에서 끄고 원래 상태로 복원 (`IrqMaskGuard`, 다른 IRQ 지연 0, ICER 뒤 DSB+ISB) ② PRIMASK 임계 구역 (간단하지만 모든 IRQ 지연) ③ 슬롯을 `const IrqBinding*` 한 워드로 만들고 바인딩은 `constexpr` 객체 (정렬된 32비트 STR 은 단일 복사 원자적, 언어 차원에서는 `std::atomic` release/acquire). 끄는 동안 온 IRQ 는 pending 으로 남았다가 복원 순간 새 바인딩으로 들어간다 — 유실 없음 (테스트). 꼬리: "듀얼 코어면?" → NVIC 마스크는 자기 코어 것뿐이라 ① 은 무효. ③ + atomic.

</details>

<details>
<summary>Q6. 등록 안 된 IRQ 나 spurious IRQ 는 어떻게 처리하나요?</summary>

두 종류를 구분한다. **등록 안 된 IRQ** (주인 없음): 기본 핸들러가 IPSR 로 번호를 기록하고 그 IRQ 를 NVIC 에서 끈다. 끄지 않으면 레벨 트리거 소스(플래그를 아무도 안 지움)가 무한 재진입해서 main 이 굶는다. 테스트: IRQ 11 을 다시 올려도 기록은 1 이고 pending 으로만 남는다. 디버그 빌드는 `bkpt` 로 멈추는 게 낫고, 현장 빌드는 기록 + 끄기 + (필요하면) 오류 보고. **spurious** (주인은 있는데 할 일이 없음): 드라이버가 상태 레지스터를 보고 센다 (`Uart::spurious`). 원인은 늦은 플래그 클리어 — 핸들러 끝에서 플래그를 지우고 바로 리턴하면 버스 쓰기가 늦게 반영돼 같은 IRQ 가 한 번 더 들어온다 (BM-M01 이 다룸). 꼬리: "IPSR 이 16 미만이면?" → 시스템 예외가 같은 함수로 온 것. `unsigned` 빼기가 감싸서 거대한 값이 되므로 범위 검사가 그것도 잡는다 (`bad_irq`).

</details>

<details>
<summary>Q7. 벡터 테이블을 C++ 로 정의할 때 함정은?</summary>

① **상수 초기화**: 엔트리가 하나라도 상수식이 아니면 동적 초기화가 될 수 있다. 실측: 함수 호출 엔트리 하나 → 테이블 전체가 0 으로 `.isr_vector` 에 놓이고, `_GLOBAL__sub_I` 가 `__aeabi_memclr4` 후 SP·Reset 을 써 넣는다. 그 생성자는 Reset 이후에야 도는데 Reset 주소가 0 이다. `reinterpret_cast` 로 SP 를 넣은 판은 clang·GCC 모두 정적으로 초기화했지만 그건 [basic.start.static]/3 의 허용일 뿐 — `constinit` 을 붙이면 `reinterpret_cast is not allowed in a constant expression` 로 거부된다. 그래서 `constinit const` + union 으로 SP 를 캐스트 없이. ② **보존**: 아무도 참조하지 않으므로 `used` + `KEEP(*(.isr_vector))`. ③ **Thumb 비트**: `&함수` 는 링커가 +1 해 준다 (실측 홀수 주소). 정수로 계산해 넣으면 빠진다. ④ **RAM 판의 정렬**: `alignas(512)` (112 엔트리). ⑤ 0 엔트리는 HardFault — 예약 슬롯만 0 이어야 한다. 꼬리: "`constexpr` 와 `constinit` 차이?" → `constexpr` 는 상수식 + const, `constinit` 은 정적 초기화만 강제하고 const 는 따로 (CPP-B09, CPP-M13).

</details>

<details>
<summary>Q8. 핸들러 테이블을 IrqHandler 인터페이스(virtual) 포인터 배열로 하면 안 되나요?</summary>

된다. 슬롯이 `IrqHandler*` 한 워드라서 오히려 등록 경쟁이 없다 (정렬된 포인터 store 하나). 디스패치는 `ldr vptr; ldr fn; blx` 로 thunk 방식과 비슷한 수의 로드다 (이 레슨에서 측정하지는 않음 — 미확인). 대가는 설계 쪽에 있다: 드라이버가 `IrqHandler` 를 상속해야 하고, 클래스마다 vtable 이 생기고 가상 함수 전체가 `--gc-sections` 에 살아남으며, 소멸자를 가상으로 두면 deleting dtor 가 `operator delete` 를 끌고 온다 — 그 회계는 CPP-M06 이 실측한다. thunk 방식은 상속 없이 아무 클래스의 아무 `void()` 멤버나 받는다 (CPP-M12 와 같은 이유). 꼬리: "한 객체가 IRQ 두 개(TX DMA, RX DMA)를 받으면?" → thunk 방식은 `attach<&Uart::on_tx_dma>`, `attach<&Uart::on_rx_dma>` 로 멤버를 고르면 끝. 인터페이스 방식은 IRQ 번호로 다시 분기해야 한다.

</details>

## 9. 추가 연습

### 연습 1 — 한 워드 게시 등록 테이블

슬롯을 `std::atomic<const IrqBinding*>` 로 바꾸고, 바인딩은 `static constexpr IrqBinding kBindA{&rec_a, &g_tag_a};` 처럼 상수로 둔다. 실행 중 교체(rebind)를 허용하고, store 앞뒤 어디서 ISR 이 와도 찢어진 바인딩이 보이지 않음을 보여라.

<details>
<summary>답 보기</summary>

```cpp
void publish(unsigned irq, const IrqBinding* b) {
    if (irq >= kNumIrq) return;
    isr_window();
    slots_[irq].store(b, std::memory_order_release);   // 정렬된 32비트 STR 하나
    isr_window();
}
void dispatch(unsigned irq) const {
    const IrqBinding* b = (irq < kNumIrq) ? slots_[irq].load(std::memory_order_acquire) : nullptr;
    if (b != nullptr) b->fn(b->obj); else ++g_default;
}
```

교체 직전 틈의 ISR 은 **옛 바인딩 전체** (A) 를, 직후는 새 바인딩 전체를 본다 — 중간 상태가 존재하지 않는다. `kBindA` 는 주소 상수라 상수 초기화되고 플래시에 놓인다 (생성 코드 없음). `static_assert(std::atomic<const IrqBinding*>::is_always_lock_free)` 로 CPP-A04 의 정책을 건다. 비용: 디스패치에 로드 1개 추가 (포인터 → 바인딩), 대가로 가드가 필요 없고 다른 코어가 같은 테이블을 읽어도 안전하다. 단일 코어 ISR ↔ main 만이면 relaxed + `atomic_signal_fence` 로도 충분하다 (CPP-A04 Q7).

</details>

### 연습 2 — 공유 IRQ 라인 (EXTI9_5 류)

한 IRQ 에 여러 소스가 묶인 라인 (STM32 의 EXTI9_5 처럼 핀 5–9 가 IRQ 하나) 을 위해 `SharedIrq<Max>` 를 만들어라: 핸들러는 `bool(void*)` ("내 것이었나"), 디스패치는 모든 핸들러를 부르고 아무도 처리 못 하면 `unhandled` 를 센다. 펜딩 레지스터는 W1C 모델.

<details>
<summary>답 보기</summary>

```cpp
void dispatch() {
    bool any = false;
    for (unsigned i = 0; i < n_; ++i) any = e_[i].fn(e_[i].obj) || any;   // 단락 평가 순서 주의
    if (!any) ++unhandled;
}
```

첫 번째 처리자에서 멈추면 안 된다 — 두 핀이 동시에 서 있을 수 있고, 남은 비트 때문에 IRQ 가 곧바로 다시 들어온다 (불필요한 재진입). `any || fn()` 로 쓰면 `any` 가 참이 된 뒤로 나머지 핸들러가 **호출되지 않는다** (단락 평가) — 그래서 `fn() || any` 순서다. 테스트: xorshift 3000 번, 핀 5/6/7 의 처리 횟수가 비트가 선 횟수와 정확히 같고, 주인 없는 핀 9 만 선 경우만 `unhandled`. 주인 없는 소스는 따로 정리해야 한다 (실제: EXTI IMR 에서 그 줄을 마스크) — 안 그러면 다른 핀이 처리될 때마다 함께 남아 있다.

</details>

### 연습 3 — 어떤 IRQ 가 중복인지 알려 주기

`has_duplicate_irq` 는 참/거짓뿐이다. 첫 번째 중복 IRQ 번호(없으면 -1)를 돌려주는 `first_duplicate_irq<Tr...>()` 를 fold expression 으로 써라. C++20 의 `static_assert` 메시지는 문자열 리터럴이어야 해서 숫자를 넣을 수 없다 — 그럼 이 값을 어떻게 에러에 드러내나?

<details>
<summary>답 보기</summary>

```cpp
template <class... Tr> constexpr int first_duplicate_irq() {
    constexpr unsigned v[] = {Tr::irq..., 0u};
    int found = -1;
    unsigned i = 0;
    ((found < 0 ? [&] { for (unsigned j = 0; j < i; ++j) if (v[j] == Tr::irq) found = static_cast<int>(Tr::irq); }() : void(),
      ++i), ...);
    return found;
}
static_assert(first_duplicate_irq<FakeTr<37>, FakeTr<38>, FakeTr<37>, FakeTr<38>>() == 37);
```

쉼표 fold 는 왼쪽부터 평가되므로 `i` 가 팩 순서대로 증가하고, 각 원소를 그 앞의 원소들과만 비교한다. 에러에 숫자를 드러내는 C++20 요령: `static_assert(first_duplicate_irq<...>() == -1)` 의 실패 메시지에 Apple clang 21 은 비교식의 **평가된 값** 을 note 로 찍는다 — 실측: `note: expression evaluates to '37 == -1'` (CPP-M14 가 static_assert 메시지 기법을 다룬다). 사용자 정의 메시지는 C++26 (P2741).

</details>

### 연습 4 — 지연 디스패치 (bottom half)

ISR 은 비트맵에 자기 IRQ 비트만 세우고 (`fetch_or`), main 루프의 `run(handle)` 이 워드별로 `exchange(0)` 해서 낮은 번호부터 처리한다. 같은 IRQ 를 두 번 표시하면 한 번 처리(합쳐짐), 처리 도중 ISR 이 새 비트를 세워도 잃어버리지 않아야 한다.

<details>
<summary>답 보기</summary>

```cpp
template <class F> unsigned run(F&& handle) {
    unsigned done = 0;
    for (unsigned w = 0; w < 3; ++w) {
        uint32_t m = bits_[w].exchange(0, std::memory_order_acquire);
        while (m != 0) {
            const unsigned b = static_cast<unsigned>(std::countr_zero(m));
            m &= m - 1;
            handle(w * 32 + b);
            ++done;
            isr_window();
        }
    }
    return done;
}
```

워드를 `exchange(0)` 로 **통째로** 가져가므로 "읽고 → 지우기" 사이에 ISR 이 세운 비트가 지워지는 일이 없다 (load 후 store(0) 이면 그 사이 비트가 유실). 이미 지나간 워드에 세워진 비트는 다음 `run` 에서, 아직 안 지난 워드면 같은 `run` 에서 처리된다 (테스트: IRQ 3 → 다음 run, IRQ 80 → 같은 run). 합쳐짐은 설계상의 성질이다 — "몇 번" 이 중요하면 카운터가 필요하다. M3/M4 에서 `fetch_or` 는 `ldrex/orr/strex`, M0+ 면 CPP-A04 의 폴백 정책 대상.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 18/18 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer practice.cpp -o practice_s && ./practice_s   # 18/18, 보고 없음
```

## 10. 핵심 정리

- 벡터 엔트리 = 1워드 C 링크 `void(void)` 주소. 멤버 함수는 `this` 가 필요하고 멤버 함수 포인터는 8 B (ARM: `{주소|vtable 오프셋, 2×adj + 가상?}`) → 사이에 **트램펄린**.
- 런타임: 공통 `extern "C" irq_trampoline` + IPSR − 16 → `{thunk, obj}` 테이블 (12 명령, 간접 분기, RAM 768 B). 컴파일 타임: `Trampoline<IRQ, 인스턴스, &멤버>::isr` 를 테이블에 직접 (3 명령, RAM 0).
- 표준: 템플릿은 C 링크 불가 [temp.pre]/6, 링크 다른 함수 타입은 다른 타입 [dcl.link]/1. clang 21·GCC 14 는 구분하지 않는다 (실측) — 이식성이 필요하면 IRQ 별 `extern "C"` 한 줄. 그 `extern "C"` 를 빼먹으면 링크 에러 없이 weak 기본 핸들러가 남고 내 핸들러는 gc 된다.
- 등록: 2워드 쓰기는 그 IRQ 를 끄고 (원래 상태로 복원) 하거나 한 워드 게시. **등록 → enable, disable → 해제**, 소멸자에서 해제.
- 등록 안 된 IRQ: 기록 + 그 IRQ disable (폭주 방지). spurious 는 드라이버가 센다.
- C++ 벡터 테이블은 `constinit const` + `used`/`KEEP`. 상수식이 아닌 엔트리 하나면 테이블 전체가 0 으로 시작할 수 있다 (`.init_array`, `__aeabi_memclr4` 실측).

## 참고자료

- C++ 표준 (N4861 C++20 초안): [temp.pre]/6 (템플릿과 C 링크), [dcl.link]/1·/5 (언어 링크와 함수 타입, 멤버에서 무시), [temp.arg.nontype] (참조·포인터 NTTP), [expr.const] (`reinterpret_cast` 금지), [basic.start.static] (정적 초기화 허용), [dcl.constinit], [temp.constr] — <https://timsong-cpp.github.io/cppwp/n4861/>
- ARMv7-M Architecture Reference Manual (DDI 0403) — 예외 진입·IPSR·벡터 테이블·VTOR: <https://developer.arm.com/documentation/ddi0403/latest/>
- Cortex-M4 Devices Generic User Guide (DUI 0553) — NVIC ISER/ICER, VTOR 정렬 요구: <https://developer.arm.com/documentation/dui0553/latest/>
- ARM Application Note 321 — 메모리 배리어 (NVIC 설정 변경 후 DSB/ISB): <https://developer.arm.com/documentation/dai0321/latest/>
- C++ ABI for the Arm Architecture (IHI 0041) — 멤버 함수 포인터 표현, `__aeabi_atexit`: <https://github.com/ARM-software/abi-aa/releases>
- Itanium C++ ABI — 멤버 함수 포인터 표현 (비교용): <https://itanium-cxx-abi.github.io/cxx-abi/abi.html#member-function-pointers>
- CMSIS-Core — `NVIC_EnableIRQ/DisableIRQ`, `__get_IPSR`, 스타트업의 weak alias 패턴: <https://arm-software.github.io/CMSIS_6/latest/Core/index.html>
- GCC 문서 — `alias`, `weak`, `section`, `used` 함수/변수 속성: <https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html>
- 관련 레슨: CPP-M12 (delegate·스텁), C-M06 (함수 포인터 테이블), BM-M01 (NVIC 레지스터·플래그 클리어 위치), BM-A01 (VTOR 재배치), BM-A03 (부트로더 → 앱 VTOR), CPP-B06 (PRIMASK RAII), CPP-B09 / CPP-M13 (정적 초기화·constinit), CPP-A03 (`__cxa_*`/`__aeabi_atexit`), CPP-A04 (ISR 공유 atomic), CPP-M06 (vtable 비용), CPP-M14 (static_assert 메시지)
