---
id: CPP-B14
time: 50분
updated: 2026-11-07
---
# CPP-B14 힙 금지 강제: operator new 삭제와 링크 에러

## 학습 목표

- 교체 가능한 전역 할당 함수(replaceable global allocation functions) **20개**(new 8 + delete 12)를 직접 정의해서, 프로그램의 모든 동적 할당을 가로챌 수 있다.
- 호스트에서 할당 횟수를 세어 **std::array · 고정 버퍼 · 정적 버퍼 placement-new = 0회**, **std::vector · SSO를 넘는 std::string · 큰 캡처 std::function = 할당 발생**을 숫자로 보인다.
- 힙 사용을 **컴파일 단계**(클래스 전용 `operator new = delete`), **링크 단계**(정의 없는 심볼, `-nostdlib++`, `--wrap=malloc`), **런타임**(abort 스텁, 카운터)에서 잡는 방법과 각각의 한계를 설명한다.
- "전역 operator new는 `= delete` 할 수 없다", "virtual 소멸자가 operator delete를 끌어온다", "-O2가 할당을 통째로 지운다" 같은 함정을 안다.

## 1. 문제

많은 펌웨어 코딩 규칙이 힙을 금지하거나 "초기화 이후 힙 금지"를 요구한다(MISRA C++:2008 Rule 18-4-1, JSF AV C++ Rule 206). C에서는 `malloc` 을 grep 하면 확인이 끝났지만, C++에서는 `v.push_back(x)`, `a + b`(문자열), `std::function f = [..]{}` **한 줄 안에 할당이 숨어 있다.** 그래서 면접 꼬리질문 단골이다: "C++ 쓰시면 힙을 안 쓴다는 걸 어떻게 보장하세요?"

숨은 할당은 결국 **전부 전역 `::operator new` 한 곳을 지나간다.** 그래서 그 지점을 세 군데에서 막을 수 있다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">숨은 할당이 지나가는 길과 세 개의 관문</text>
  <rect x="10" y="50" width="190" height="96" rx="6" fill="none" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="74">v.push_back(x);</text>
    <text x="20" y="98">s = "id=" + name;</text>
    <text x="20" y="122">fn = [big]{ ... };</text>
  </g>
  <rect x="240" y="70" width="150" height="56" rx="6" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="250" y="93">std::allocator&lt;T&gt;</text>
    <text x="250" y="113">__func::__clone</text>
  </g>
  <rect x="430" y="70" width="150" height="56" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="442" y="103" fill="currentColor" font-family="monospace" font-size="12">::operator new(n)</text>
  <rect x="620" y="70" width="110" height="56" rx="6" fill="none" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="630" y="93">malloc→_sbrk</text>
    <text x="630" y="113">.heap 영역</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M200 98 L236 98"/><path d="M230 93 L236 98 L230 103"/>
    <path d="M390 98 L426 98"/><path d="M420 93 L426 98 L420 103"/>
    <path d="M580 98 L616 98"/><path d="M610 93 L616 98 L610 103"/>
  </g>
  <g stroke="currentColor" stroke-dasharray="5 4">
    <line x1="220" y1="44" x2="220" y2="170"/>
    <line x1="410" y1="44" x2="410" y2="170"/>
    <line x1="600" y1="44" x2="600" y2="170"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-weight="bold">
    <text x="220" y="188">①</text><text x="410" y="188">②</text><text x="600" y="188">③</text>
  </g>
  <rect x="10" y="202" width="720" height="88" rx="6" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="22" y="226">① 컴파일: 클래스 전용 operator new = delete → new UartDriver 만 막음 (vector&lt;T&gt; 는 통과)</text>
    <text x="22" y="250">② 링크: operator new 정의가 없게(-nostdlib++) / 정의 없는 심볼을 부르게(poison) / --wrap=malloc</text>
    <text x="22" y="274">③ 런타임: operator new 를 abort 스텁 · 카운터로 교체 → 이번 레슨의 측정 하네스</text>
  </g>
</svg>
```

### Part A — 세는 operator new / delete (20개 오버로드)

`tracked_alloc(n, align, nothrow)` / `tracked_free(p)` 두 함수에 통계(`news`, `deletes`, `aligned_news`, `nothrow_news`)를 세는 코드를 넣고, 시작 코드에 4개만 있는 전역 오버로드를 **20개 전부**로 늘린다. 정렬 64바이트 객체(`alignas(64)` DMA 디스크립터)의 `new` 와 `new (std::nothrow)` 도 빠짐없이 세어져야 한다.

### Part B — 무엇이 할당하나 측정

`count_allocs(f)` — `f()` 실행 동안 일어난 `operator new` 호출 수를 돌려준다. 이것으로 `std::array`, 짧은/긴 `std::string`, `std::vector`(reserve 유무), 작은/큰 캡처 `std::function`, placement new를 측정해 표를 채운다.

### Part C — 힙 금지 구역과 펌웨어식 코드

- `NoHeapGuard` — RAII(CPP-B06 참고)로 "이 범위에서 할당 = 위반"을 켜고, 끝나면 **이전 상태로** 복원한다(중첩 가능).
- `UartDriver` 를 `new` 로 만들 수 없게 클래스 전용 `operator new` 를 `= delete` 한다.
- `uart_instance()` — `.bss` 의 정적 버퍼 `g_uart_storage` 위에 처음 한 번만 placement new로 생성한다.
- 가드 안에서 `firmware_tick()`(std::array 링버퍼 + 함수 포인터 콜백) 1000회 = **위반 0**, `desktop_tick()`(vector + string + function) = **위반 발생**을 확인한다.

## 2. Problem (English)

> **Prove that firmware code never touches the heap.**
>
> On a host build (`-fno-exceptions -fno-rtti`), replace every replaceable global allocation/deallocation function (`operator new`/`new[]` with `std::align_val_t` and `std::nothrow_t` variants, and the plain/sized/aligned/nothrow `operator delete`/`delete[]`) with counting versions that forward to `malloc`/`posix_memalign`/`free`.
>
> 1. Write `template <class F> unsigned count_allocs(F&& f)` returning the number of allocations performed while running `f`.
> 2. Show that `std::array`, a short `std::string`, a small-capture `std::function`, and placement new into a static buffer perform **0** allocations, while `std::vector` growth, a 38-char `std::string`, and a `std::function` holding a 64-byte capture **do** allocate.
> 3. Implement an RAII `NoHeapGuard` (nestable) that turns any allocation into a recorded violation, then show a driver built on a static buffer runs 1000 ticks with zero violations.
> 4. Discuss how you would turn heap use into a **build failure** on a Cortex-M target (`arm-none-eabi-g++`). Why can't you write `void* operator new(std::size_t) = delete;`?

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 문제 회피
F="-std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti"
c++ $F starter.cpp -o /tmp/b14_s && /tmp/b14_s
# 처음엔 FAIL 이 13줄, "12/25 passed" — TODO를 채워 25/25 를 만든다
```

> zsh 에서는 `$F` 가 단어로 쪼개지지 않는다. `bash` 에서 실행하거나 레슨 폴더의 `bash build.sh` 를 쓴다(하네스 3개 + 데모 전부).

## 4. 힌트

<details>
<summary>힌트 1 — 20개를 외우는 법</summary>

new: {단일, 배열} × {기본, `align_val_t`, `nothrow_t`, `align_val_t`+`nothrow_t`} = 8.
delete: {단일, 배열} × {기본, `size_t`, `align_val_t`, `size_t`+`align_val_t`, `nothrow_t`, `align_val_t`+`nothrow_t`} = 12.
모두 한 줄짜리로 `tracked_alloc` / `tracked_free` 에 넘기면 된다. nothrow 판과 delete 는 `noexcept`.

</details>

<details>
<summary>힌트 2 — 정렬 오버로드는 언제 불리나</summary>

`alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__`(macOS arm64 에서 16) 인 타입을 `new` 하면 컴파일러가 `operator new(size_t, std::align_val_t)` 를 부른다(C++17, P0035). 그래서 `align > 16` 이면 `aligned_news++`.

</details>

<details>
<summary>힌트 3 — count_allocs 는 "차이"를 잰다</summary>

절대값은 런타임 시작 코드가 이미 할당했을 수도 있어서 믿을 수 없다. `before = g_heap.news; f(); return g_heap.news - before;`. 측정 대상 객체는 `escape(obj)` 로 바깥에 흘려야 최적화기가 할당을 지우지 않는다.

</details>

<details>
<summary>힌트 4 — 가드와 정적 생성</summary>

가드는 생성자에서 이전 값을 저장하고 `true`, 소멸자에서 저장한 값으로 되돌린다(그냥 `false` 로 두면 중첩 시 바깥 가드가 풀린다). 정적 생성은 `::new (static_cast<void*>(g_uart_storage)) UartDriver(&fake_uart1)` — 앞의 `::` 가 중요하다(풀이 Part C).

</details>

## 5. 풀이

### Part A — 교체 가능한 할당 함수 20개

표준([replacement.functions], [new.delete.single], [new.delete.array])은 프로그램이 아래 함수를 **직접 정의하면 라이브러리 기본 버전 대신 그것을 쓴다**고 정한다. 링커가 아니라 언어 규칙이 보장하는 "교체(replacement)"다.

| 묶음 | 단일 객체 | 배열 |
|---|---|---|
| new 기본 | `operator new(size_t)` | `operator new[](size_t)` |
| new 정렬 (C++17) | `operator new(size_t, align_val_t)` | `operator new[](size_t, align_val_t)` |
| new nothrow | `operator new(size_t, const nothrow_t&) noexcept` | `[]` 판 |
| new 정렬+nothrow | `operator new(size_t, align_val_t, const nothrow_t&) noexcept` | `[]` 판 |
| delete 기본 / sized (C++14) | `operator delete(void*)` / `(void*, size_t)` | `[]` 판 2개 |
| delete 정렬 / sized+정렬 | `(void*, align_val_t)` / `(void*, size_t, align_val_t)` | `[]` 판 2개 |
| delete nothrow / 정렬+nothrow | `(void*, const nothrow_t&)` / `(void*, align_val_t, const nothrow_t&)` | `[]` 판 2개 |

교체 **불가**: placement 형태 `operator new(size_t, void*)` 등 4개는 "reserved" — 프로그램이 정의하면 안 된다([new.delete.placement]). 그래서 placement new는 항상 "할당 없음"이다.

표준의 **기본 동작**은 서로를 부르도록 정해져 있다. 뿌리는 4개뿐이다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">표준 기본 동작: 화살표 = "기본 구현이 이것을 호출"</text>
  <g font-family="monospace">
    <rect x="270" y="40" width="190" height="30" rx="5" class="f-acc" stroke="currentColor"/>
    <text x="282" y="60" fill="currentColor">new(n)</text>
    <rect x="270" y="90" width="190" height="30" rx="5" class="f-acc" stroke="currentColor"/>
    <text x="282" y="110" fill="currentColor">new(n, al)</text>
    <rect x="270" y="150" width="190" height="30" rx="5" class="f-acc" stroke="currentColor"/>
    <text x="282" y="170" fill="currentColor">delete(p)</text>
    <rect x="270" y="200" width="190" height="30" rx="5" class="f-acc" stroke="currentColor"/>
    <text x="282" y="220" fill="currentColor">delete(p, al)</text>
    <g fill="currentColor">
      <text x="10" y="50">new[](n)</text><text x="10" y="66">new(n, nothrow)</text>
      <text x="10" y="100">new[](n, al)</text><text x="10" y="116">new(n, al, nothrow)</text>
      <text x="10" y="160">delete(p, sz), delete[](p)</text><text x="10" y="176">delete(p, nothrow)</text>
      <text x="10" y="210">delete(p, sz, al)</text><text x="10" y="226">delete(p, al, nothrow)</text>
    </g>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M215 55 L266 55"/><path d="M260 50 L266 55 L260 60"/>
    <path d="M215 105 L266 105"/><path d="M260 100 L266 105 L260 110"/>
    <path d="M215 165 L266 165"/><path d="M260 160 L266 165 L260 170"/>
    <path d="M215 215 L266 215"/><path d="M260 210 L266 215 L260 220"/>
  </g>
  <rect x="480" y="40" width="250" height="190" rx="6" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="492" y="64" font-weight="bold">그런데도 20개를 다 정의하는 이유</text>
    <text x="492" y="90">• nothrow · 정렬 경로를 따로 세려고</text>
    <text x="492" y="114">• sized delete 사용 여부가 컴파일러</text>
    <text x="492" y="132">  설정마다 다름 (아래 실측)</text>
    <text x="492" y="156">• 구버전 라이브러리는 nothrow 판이</text>
    <text x="492" y="174">  교체본을 안 거치기도 했다</text>
    <text x="492" y="198">• 정렬 new 의 기본 구현은 new(n) 을</text>
    <text x="492" y="216">  부르지 않는다 → 직접 정의 필수</text>
  </g>
</svg>
```

시작 코드는 기본 4개(`new`, `new[]`, `delete`, `delete[]`)만 정의돼 있어서, `new DmaDescriptor`(alignas 64)는 라이브러리 자체의 정렬 new로 가서 **세어지지 않는다** — Part A 의 aligned 테스트가 FAIL 하는 이유다.

sized delete가 정말 컴파일러마다 다른지 실측했다 (Apple clang 21):

```text
$ echo | c++ -std=c++20 -x c++ -dM -E - | grep -i sized   → (아무것도 없음: __cpp_sized_deallocation 미정의)
$ ... <new> 포함 시: #define _LIBCPP_HAS_SIZED_DEALLOCATION 0
-O2 기본         : new Big64 → new=1 sized_del=0
-O2 -fsized-deallocation : new Big64 → new=1 sized_del=1
```

Apple clang 은 기본값이 **꺼짐**, 업스트림 Clang 19+ 와 GCC(C++14 이상)는 **켜짐**이다. `delete(void*, size_t)` 를 빼먹으면 어떤 툴체인에서는 표준 기본판(→ 우리 `delete(void*)`)을 거쳐 문제가 없지만, 그걸 믿지 말고 다 정의하는 게 안전하다.

구현할 때 지켜야 할 규칙:

| 규칙 | 근거 / 이유 |
|---|---|
| 크기 0 요청도 **고유한 non-null** 포인터 | [basic.stc.dynamic.allocation] → `if (n == 0) n = 1;` |
| throwing 판은 **절대 nullptr 반환 금지** | `new T` 는 결과를 null 검사하지 않는다. clang 은 `-Wnew-returns-null` 로 막는다(실제로 이 경고에 걸렸다). `-fno-exceptions` 면 `bad_alloc` 대신 `abort()` |
| nothrow 판은 실패 시 nullptr | 그래서 `if (!p && !nothrow) abort();` |
| `inline` 으로 선언 금지 | [replacement.functions] "shall not be declared inline" |
| 정렬 > 16 은 `posix_memalign` | 펌웨어라면 정렬 풀(C-A05 참고) |

**macOS 관찰**: `std::string` 의 일부 멤버(`__init`, `~basic_string`)는 libc++.dylib 안에 미리 인스턴스화돼 있다(`nm -u` 로 확인). 그런데도 긴 문자열 할당이 우리 카운터에 잡혔다 → dylib 내부 호출도 실행 파일의 교체본으로 바인딩된다. (`-O0` 빌드에서는 dylib 의 `~basic_string` 이 sized delete 를 불렀다 — dylib 는 sized deallocation 을 켜고 빌드됐다는 뜻.)

### Part B — 무엇이 할당하나 (실측)

`solution.cpp` 출력과 별도 실험(`-O2`, Apple clang 21 / libc++ 21, arm64):

| 코드 | operator new 횟수 | 왜 |
|---|---|---|
| `std::array<int,64> a{}` | **0** | 스택에 그대로 (CPP-B10 참고) |
| `std::string s("short-msg")` (9자) | **0** | SSO(small string optimization): 객체 안 버퍼 |
| `std::string` 22자 / 23자 | **0 / 1** | libc++ 64비트 SSO 한계 22자 (libstdc++ 는 15자) |
| `std::string` 38자 | **1** | 힙 버퍼 |
| `vector<int>` push_back ×5 | **4** | 용량 1→2→4→8, 늘 때마다 새 버퍼 |
| `reserve(8)` 후 ×5 | **1** | 한 번에 확보 |
| `std::function` 캡처 4B / 16B | **0 / 0** | SBO(small buffer optimization) |
| `std::function` 캡처 24B / 64B | **1 / 1** | libc++ `sizeof(std::function)=32`, 버퍼 24B 중 vtable 포인터 몫 빼면 16B |
| placement new into static buffer | **0** | 메모리를 우리가 줬다 |

`std::function` 의 SBO 크기, SSO 길이, 성장 배수는 **표준이 정하지 않은 구현 사항**이다. 하네스는 그래서 `a_vec >= 2` 처럼 관계만 검사하고 실제 숫자는 출력만 한다. 32비트 Cortex-M 의 libstdc++ 라면 `std::function` 내부 버퍼가 포인터 2개(8바이트)라 더 쉽게 할당한다 (예상, 실행 못 함).

**함정 — 할당 소거(allocation elision)**: C++14부터 구현은 교체 가능한 할당 함수 호출을 **생략해도 된다**([expr.new], N3664). 실제로:

```text
new int(5); 결과를 volatile 로 한 번 읽고 delete   -O0: new=1   -O2: new=0
vector<int> v; v.push_back(1);  (escape 없음)      -O2: 바이너리에 operator new 참조 자체가 없음 (nm 확인)
vector<int> push_back ×5        (escape 없음)      -O2: new=4  (이번엔 안 지움 — 휴리스틱)
```

그래서 측정 하네스는 `escape(obj)`(빈 `asm volatile` 에 주소를 넘김)로 "이 객체는 밖에서 쓰인다"고 컴파일러를 속인다. 거꾸로 말하면 **"-O2 에서 카운트 0" 은 "코드가 힙을 안 쓴다"는 증명이 아니다** — 최적화 수준이 바뀌면 다시 나타난다. 펌웨어 검증은 할당이 일어날 수 있는 *코드 경로*를 막아야 한다(링크 단계).

### Part C — 힙 금지 구역 + 0-할당 대안

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">firmware_tick 은 어디에 메모리를 두나</text>
  <rect x="10" y="40" width="340" height="190" rx="6" fill="none" stroke="currentColor"/>
  <text x="22" y="62" fill="currentColor" font-weight="bold">정적 / 스택 (크기가 링크 때 확정)</text>
  <rect x="22" y="76" width="316" height="40" class="f-ok" stroke="currentColor"/>
  <text x="32" y="93" fill="currentColor" font-family="monospace" font-size="12">.bss g_uart_storage[sizeof(UartDriver)]</text>
  <text x="32" y="109" fill="currentColor" font-family="monospace" font-size="12">  ← ::new(storage) UartDriver(&amp;uart1)</text>
  <rect x="22" y="124" width="316" height="40" class="f-ok" stroke="currentColor"/>
  <text x="32" y="141" fill="currentColor" font-family="monospace" font-size="12">.bss SensorLog: array&lt;uint16_t,32&gt;</text>
  <text x="32" y="157" fill="currentColor" font-family="monospace" font-size="12">  링버퍼, 넘치면 덮어씀</text>
  <rect x="22" y="172" width="316" height="44" class="f-ok" stroke="currentColor"/>
  <text x="32" y="190" fill="currentColor" font-family="monospace" font-size="12">stack frame: array&lt;uint8_t,3&gt;</text>
  <text x="32" y="207" fill="currentColor" font-family="monospace" font-size="12">callback: fn ptr + void* ctx</text>
  <rect x="390" y="40" width="340" height="190" rx="6" fill="none" stroke="currentColor"/>
  <text x="402" y="62" fill="currentColor" font-weight="bold">.heap (펌웨어에선 크기 0 이어야)</text>
  <rect x="402" y="76" width="316" height="100" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="412" y="96">desktop_tick:</text>
    <text x="412" y="116">  new vector        → 1</text>
    <text x="412" y="134">  vector 버퍼       → 1</text>
    <text x="412" y="152">  29자 string       → 1</text>
    <text x="412" y="170">  function(64B 캡처) → 1</text>
  </g>
  <text x="402" y="200" fill="currentColor">가드 안에서 violations = 4 (실측)</text>
  <text x="402" y="220" fill="currentColor">펌웨어라면 첫 번째에서 트랩</text>
</svg>
```

**NoHeapGuard** 는 CriticalSection 가드(CPP-B06)와 같은 "저장 → 설정 → 복원" 패턴이다. 복원을 `false` 로 하드코딩하면 안쪽 가드가 끝나는 순간 바깥 가드까지 풀린다 — 테스트 `{ NoHeapGuard outer; { NoHeapGuard inner; } CHECK(g_forbid_heap); }` 가 이것을 잡는다. 복사 금지는 CPP-B07 참고.

**클래스 전용 operator new = delete** 는 어떤 범위까지 막을까? [expr.new]: `new T` 가 `::` 없이 쓰이면 할당 함수 이름을 **먼저 T 의 클래스 범위에서** 찾고, 찾으면 전역은 보지 않는다.

| 식 | 결과 | 이유 |
|---|---|---|
| `new UartDriver(...)` | 컴파일 에러 (deleted) | 클래스 범위에서 deleted 함수를 찾음 |
| `new (buf) UartDriver(...)` | 컴파일 에러 (no matching) | 클래스 범위에 2-인자 판이 없고, 전역 placement 는 가려짐 |
| `::new (buf) UartDriver(...)` | OK | `::` 는 전역만 찾는다 |
| `std::construct_at(p, ...)` | OK | 내부가 `::new` |
| `std::make_unique<UartDriver>` | 컴파일 에러 (실측) | 내부가 `new T(...)` |
| `std::vector<UartDriver>` | **컴파일됨** (실측) | `std::allocator` 는 `::operator new` 로 바이트만 받아 온다 |

즉 클래스 전용 삭제는 "이 타입을 개별 `new` 하지 마라"는 *설계 의도 표시*이지 힙 금지 장치는 아니다. 하네스는 `concept heap_newable = requires { new T; };` 로 이것을 런타임 CHECK 로 보고한다(deleted 함수를 쓰는 식은 requires 안에서 "불가"로 평가된다).

**정적 버퍼 placement new** 는 C 에서 `static uart_t g_uart; uart_init(&g_uart, regs);` 하던 것과 같은 일이다. 생성 시점을 직접 고를 수 있다는 점(전역 객체 생성자 순서 문제를 피함, CPP-B09 참고)만 다르다. 수명·소멸·재생성까지 다루는 건 CPP-M07, 풀(pool)은 CPP-M08, 고정 용량 vector 는 CPP-M10, 무힙 콜백은 CPP-M12 에서 한다. 여기서는 "0-할당 대안이 존재한다"까지만.

### 빌드 단계에서 막기 — 데모 3개 (맥 실측)

전부 `bash build.sh` 한 번으로 재현된다. `$F` 는 위의 기본 플래그. 데모 파일: `demo_abort_stub.cpp`, `demo_link_error.cpp`, `demo_poison_new.cpp` + `demo_poison_app.cpp`, `demo_delete_error.cpp`.

**① abort 스텁** — `demo_abort_stub.cpp`: 20개 전부를 "메시지 + `abort()`" 로 정의.

```text
$ c++ $F demo_abort_stub.cpp -o /tmp/b14_abort && /tmp/b14_abort
fixed-buffer sum = 36
done, no heap used                       (exit 0)

$ c++ $F -DUSE_HEAP demo_abort_stub.cpp -o /tmp/b14_abort && /tmp/b14_abort
FATAL: heap use forbidden: operator new(32)
fixed-buffer sum = 36
Abort trap: 6                            (exit 134)
```

stdout 이 파이프일 때 완전 버퍼링이라 `abort()` 직전 printf 가 늦게 찍혀 순서가 뒤바뀌어 보인다(stderr 는 즉시). 펌웨어라면 `abort()` 대신 `__BKPT(0)` 이나 fault 로그 후 리셋. **장점**: 어떤 툴체인에서도 동작. **단점**: 그 경로가 *실행돼야* 발견된다.

**② 링크 에러** — `demo_link_error.cpp`. 방법 두 가지:

```text
# 방법 1: operator new 가 "정의 없는 함수"를 부르게 정의 + dead strip
$ c++ $F -DPOISON -Wl,-dead_strip demo_link_error.cpp -o /tmp/b14_link && /tmp/b14_link
led=1                                    ← 아무도 new 를 안 쓰면 operator new 가 통째로 제거돼 링크 OK
$ c++ $F -DPOISON -DSHOW_ERROR -Wl,-dead_strip demo_link_error.cpp -o /tmp/b14_link
Undefined symbols for architecture arm64:
  "_HEAP_IS_FORBIDDEN_see_CPP_B14", referenced from:
      _main in demo_link_error-b31d24.o
ld: symbol(s) not found for architecture arm64

# 방법 2: libc++ 를 링크하지 않음 → <new> 에 선언만 있고 정의가 없다
$ c++ $F -nostdlib++ demo_link_error.cpp -o /tmp/b14_link && /tmp/b14_link
led=1
$ c++ $F -nostdlib++ -DSHOW_ERROR demo_link_error.cpp -o /tmp/b14_link
Undefined symbols for architecture arm64:
  "operator new(unsigned long)", referenced from:
      _main in demo_link_error-6f15e8.o
      _main in demo_link_error-6f15e8.o
      _main in demo_link_error-6f15e8.o
      std::__1::__function::__func<main::$_0, int ()>::__clone() const in demo_link_error-6f15e8.o
ld: symbol(s) not found for architecture arm64
```

방법 1 에서 `-dead_strip` 을 빼면 사용 여부와 상관없이 항상 실패한다(4개 operator new 가 모두 참조를 남김 — 실측). 심볼 이름 자체를 에러 메시지로 쓰는 트릭이다. 같은 TU 에선 operator new 가 main 에 인라인돼 "referenced from: _main" 으로 나왔지만, 실제 프로젝트처럼 poison 을 별도 파일(`demo_poison_new.cpp`)에 두면 에러는 "operator new 안에서 참조"라고만 말한다. **누가** 불렀는지는 ld64 의 `-why_live` 로 본다 (실측, 임시 경로 생략):

```text
$ c++ $F -Wl,-dead_strip demo_poison_new.cpp demo_poison_app.cpp -o /tmp/b14_two
Undefined symbols for architecture arm64:
  "_HEAP_IS_FORBIDDEN_see_CPP_B14", referenced from:
      operator new(unsigned long) in demo_poison_new-4d30b7.o

$ c++ $F -Wl,-dead_strip -Wl,-why_live,__Znwm demo_poison_new.cpp demo_poison_app.cpp -o /tmp/b14_two
operator new(unsigned long) from demo_poison_new-093c3c.o
  log_sample(int) from demo_poison_app-837a2f.o
    _main from demo_poison_app-837a2f.o
Undefined symbols for architecture arm64: ... (위와 같음)
```

**함정 — 힙을 안 써도 operator delete 가 필요하다**:

```text
$ c++ $F -nostdlib++ -DNO_DELETE_STUBS demo_link_error.cpp -o /tmp/b14_link
Undefined symbols for architecture arm64:
  "operator delete(void*)", referenced from:
      Led::~Led() in demo_link_error-12e617.o
```

`Led` 는 **정적 객체**인데도 virtual 소멸자가 있으면 컴파일러가 vtable 에 "deleting destructor"(Itanium ABI 의 D0, `delete p` 용)를 넣고, 그것이 `operator delete` 를 참조한다. 그래서 링크 에러 방식에서도 delete 는 "정의는 하되 트랩"으로 둔다 (`if (p) abort();`). `__cxa_pure_virtual` 같은 다른 런타임 심볼은 CPP-A03 참고.

**③ = delete 가 되는 곳/안 되는 곳** — `demo_delete_error.cpp -DSHOW_ERROR` (실제 에러, 경로 축약):

```text
demo_delete_error.cpp:12:35: error: deleted definition must be first declaration
   12 | void* operator new(std::size_t) = delete;
.../c++/v1/__new/global_new_delete.h:37:54: note: previous declaration is here
demo_delete_error.cpp:32:21: error: call to deleted function 'operator new'
   32 |     UartDriver* h = new UartDriver(&fake_dr);
demo_delete_error.cpp:33:21: error: no matching function for call to 'operator new'
   33 |     UartDriver* q = new (static_cast<void*>(storage)) UartDriver(&fake_dr);
note: candidate function not viable: requires 1 argument, but 2 were provided
```

전역 `operator new(size_t)` 는 **모든 TU 에 암시적으로 선언**되어 있고([basic.stc.dynamic], `<new>` 를 안 넣어도), deleted 정의는 첫 선언이어야 한다([dcl.fct.def.delete]). 그래서 전역 삭제는 원천적으로 불가능하다. 과제 제목의 "operator new 삭제"는 실제로는 **정의를 없애거나(링크 에러) 스텁으로 바꾸는 것**을 뜻한다.

### arm-none-eabi-g++ (Cortex-M) 에서는 — 예상, 이 맥에서 실행 못 함

| 방법 | 기대 동작 |
|---|---|
| abort 스텁 | 그대로 동작. 덤으로 **코드 크기 감소**: libstdc++ 의 기본 `operator new` 는 실패 시 `bad_alloc` 을 throw 하도록 (예외 켠 채) 빌드돼 있어 `-fno-exceptions` 앱에서도 unwinder · `__cxa_throw` 를 끌어온다. 직접 정의하면 그 의존이 끊긴다 (CPP-B13, CPP-A03 참고) |
| poison + gc | `-ffunction-sections -Wl,--gc-sections` 로 같은 효과. GNU ld 는 버려진 섹션의 재배치를 처리하지 않으므로 미정의 참조도 보고하지 않는다. 호출자 추적은 `-Wl,--trace-symbol=_Znwj` 또는 `-Wl,-Map=fw.map,--cref` |
| 정의 없음 | `arm-none-eabi-gcc` 드라이버로 링크(= `-lstdc++` 없음)하거나 `-nostdlib` → `undefined reference to 'operator new(unsigned int)'` |
| `--wrap=malloc` | `-Wl,--wrap=malloc,--wrap=_malloc_r,--wrap=calloc,--wrap=realloc` 하고 `__wrap_malloc` 을 **정의하지 않음** → 모든 malloc 참조가 `undefined reference to '__wrap_malloc'`. newlib 의 `printf("%f")`, `strdup`, `fopen` 처럼 **C 쪽 숨은 힙**(`_malloc_r`)까지 잡힌다. (ld64 에는 `--wrap` 이 없다) |
| `_sbrk` 가 -1 | `_sbrk` 를 `errno = ENOMEM; return (void*)-1;` 로 정의 → 링크는 되고 런타임에 malloc 이 NULL → 우리 operator new 가 트랩. 링커 스크립트 `_Min_Heap_Size = 0`(STM32CubeIDE) 와 짝 |
| 심볼 스캔 (CI) | `arm-none-eabi-nm -C fw.elf \| grep -E "operator new\|malloc\|_sbrk"`. 32비트 ARM 에선 `size_t = unsigned int` 라 mangled 이름이 `_Znwj` (맥 Mach-O 는 `__Znwm`, 밑줄 하나 더) |

**C 와 비교**: C 에서는 공통 헤더에 `#pragma GCC poison malloc calloc realloc free` 를 두면 *소스에 적힌* 호출을 컴파일 에러로 만든다. 하지만 C++ 의 할당은 라이브러리 템플릿 안(`std::allocator`)에 있어 소스 토큰 검사로는 잡히지 않는다 — 그래서 C++ 에서는 **심볼(operator new) 단위**로 막는 링크 단계 방법이 기본이다. 비용: 링크 방법과 abort 스텁은 런타임 비용 0, 카운터는 할당 1회당 증가 몇 번(호스트 테스트 전용).

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ $F solution.cpp -o /tmp/b14_s && /tmp/b14_s
#   array<int,64>=0  string(short)=0  string(long)=1  vector x5=4  reserve(8)+x5=1
#   function(4B capture)=0  function(64B capture)=1  placement-new=0
#   desktop_tick violations=4
# 25/25 passed
c++ -std=c++20 -g -O1 -fsanitize=address,undefined -fno-exceptions -fno-rtti solution.cpp -o /tmp/b14_asan && /tmp/b14_asan   # 25/25, 경고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `operator new(size_t)` 만 교체 | 정렬 new(alignas &gt; 16)는 라이브러리 기본판이 직접 할당 → 안 세어짐 / 안 막힘 | 20개 전부 정의, 최소한 `new(n, align_val_t)` 와 `delete(p, align_val_t)` 포함 |
| throwing `operator new` 에서 `return nullptr;` | `new T` 는 null 검사 없이 생성자를 호출 → UB. clang `-Wnew-returns-null` | 실패 시 `abort()` / 트랩, `[[noreturn]]` 함수 호출 |
| `void* operator new(size_t) = delete;` | 모든 TU 에 암시적 선언이 있어 "deleted definition must be first declaration" | 정의 제거(링크 에러) 또는 abort 스텁 |
| 링크 에러 방식에서 delete 까지 지움 | virtual 소멸자의 deleting destructor 가 `operator delete` 참조 → 힙 안 써도 링크 실패 | delete 는 `if (p) abort();` 로 정의 |
| 카운트 테스트를 `-O2` 에서 escape 없이 | allocation elision 으로 0 이 나와 "할당 없음"으로 오판 | `escape(obj)` 또는 `-O0` 교차 확인, 최종 판정은 링크 단계 |
| 짧은 문자열로만 테스트 | SSO 덕분에 테스트에선 0, 현장에서 긴 메시지가 오면 할당 | 길이 상한을 두는 고정 버퍼(`std::array<char,N>`) 사용 |
| `NoHeapGuard` 소멸자에서 `g_forbid = false` | 중첩된 바깥 가드까지 풀림 | 생성 시 이전 값 저장 → 소멸 시 복원 |
| 클래스 `operator new = delete` 로 힙 금지 끝 | `std::vector<T>` 는 `::operator new` 로 바이트를 받아 그대로 컴파일됨 | 전역(링크) 단계 장치와 함께 사용 |

## 8. 면접 Q&A

<details>
<summary>Q1. 펌웨어에서 왜 힙을 피하나요? 절대 금지인가요?</summary>

① 단편화(fragmentation): 총 여유는 충분한데 연속 블록이 없어 수일 뒤 실패 ② 할당 시간이 비결정적(실시간 마감 위반) ③ 메모리 부족이 정적 분석(map 파일)으로 안 보임 ④ MMU 없는 MCU 에서는 힙-스택 충돌이 조용한 메모리 오염. 절대 금지만 있는 건 아니다: "초기화 때만 할당, 이후 금지"(JSF AV Rule 206), 고정 크기 풀(CPP-M08), 정적 버퍼 위 `std::pmr` 아레나(추가 연습 5) 같은 결정적 대안이 흔하다.

</details>

<details>
<summary>Q2. C++ 코드가 힙을 안 쓴다는 걸 어떻게 보장하나요?</summary>

여러 겹으로: (1) **링크** — operator new/malloc 정의를 없애거나 poison 심볼 / `--wrap=malloc` 으로 *사용 경로가 존재하면* 빌드 실패 (2) **런타임** — 그래도 남는 경로는 operator new 를 트랩 스텁으로 (3) **CI** — `nm` 으로 최종 ELF 에 `_Znwj`/`malloc`/`_sbrk` 가 없는지 검사 (4) **호스트 단위 테스트** — 교체한 operator new 로 할당 횟수 0 단정 (5) **컴파일** — 특정 타입은 클래스 전용 `operator new = delete`. 꼬리질문 "그럼 -O2 에서 카운트 0 이면 충분?" → 아니다, allocation elision 때문에 경로가 숨을 수 있다.

</details>

<details>
<summary>Q3. 전역 operator new 를 = delete 하면 안 되나요?</summary>

안 된다. 전역 `operator new(size_t)` 등은 `<new>` 없이도 모든 번역 단위에 암시적으로 선언되고([basic.stc.dynamic]), deleted 정의는 그 함수의 첫 선언이어야 한다([dcl.fct.def.delete]). 실제 에러: "deleted definition must be first declaration". 클래스 범위에서는 가능하지만 그 타입의 `new T` 만 막는다(`std::vector<T>` 는 통과).

</details>

<details>
<summary>Q4. operator new 를 교체할 때 몇 개를 정의해야 하나요? placement new 도 바꾸나요?</summary>

교체 가능한 것은 20개(new 8, delete 12). 표준 기본 동작상 뿌리는 `new(n)`, `new(n, align)`, `delete(p)`, `delete(p, align)` 4개라 이론상 4개로도 잡히지만, sized deallocation 기본값이 컴파일러마다 다르고(Apple clang 21 은 꺼짐을 실측, GCC 는 켜짐), nothrow 경로를 따로 보고 싶으니 20개 다 정의한다. placement 형태(`new(size_t, void*)`)는 교체 금지(reserved)이고 메모리를 할당하지도 않는다. 교체본은 `inline` 으로 선언하면 안 된다.

</details>

<details>
<summary>Q5. -fno-exceptions 빌드에서 operator new 가 메모리를 못 얻으면?</summary>

표준 throwing 판은 `bad_alloc` 을 던져야 하지만 예외가 없으니 보통 `abort()`/트랩으로 끝낸다. 절대 nullptr 를 반환하면 안 된다 — `new T` 식은 결과가 non-null 이라 가정하고 바로 생성자를 부른다. 실패를 값으로 받고 싶으면 `new (std::nothrow) T` (nullptr 반환, 우리 교체본에서도 `nothrow` 플래그로 처리). 꼬리: libstdc++ 기본 operator new 는 예외 켠 채 빌드돼 있어 `-fno-exceptions` 앱에도 unwinder 를 끌어올 수 있다 → 직접 정의하는 이유 중 하나 (예상, CPP-B13 참고).

</details>

<details>
<summary>Q6. std::function 은 항상 힙을 쓰나요? 대안은?</summary>

아니다. 작은 호출 객체는 내부 버퍼(SBO)에 저장한다. 이 맥(libc++)에서 캡처 16바이트까지 0회, 24바이트부터 1회로 측정됐다. 크기는 구현 정의라 이식성 있게 믿을 수 없다. 대안: 함수 포인터 + `void*` 컨텍스트(C 스타일, 이번 Part C), 템플릿 파라미터로 호출 객체 받기, 비소유 `function_ref`(C++26 `std::function_ref`), 고정 버퍼 `inplace_function` (CPP-M12).

</details>

<details>
<summary>Q7. 정적 객체만 쓰는데 "undefined reference to operator delete" 링크 에러가 납니다. 왜죠?</summary>

virtual 소멸자가 있는 클래스는 vtable 에 deleting destructor(D0)가 들어가고 그 안에서 `operator delete` 를 부른다. 객체를 `delete` 하지 않아도 vtable 이 참조하므로 링커가 요구한다. 해결: `operator delete` 를 트랩으로 정의(`if (p) abort();`), 또는 인터페이스에서 virtual 소멸자를 protected non-virtual 로 바꾸는 설계(정적 객체만 쓰는 경우). 맥에서 `-nostdlib++ -DNO_DELETE_STUBS` 로 재현했다.

</details>

<details>
<summary>Q8. 단위 테스트에서 vector 인데 할당 0 이 나왔습니다. 버그인가요?</summary>

allocation elision 일 수 있다. C++14 부터 구현은 교체 가능한 할당 함수 호출을 생략할 수 있다([expr.new]). clang `-O2` 에서 `push_back(1)` 한 번 하고 버리는 vector 는 operator new 참조 자체가 사라졌다(실측). 측정할 땐 객체를 `asm volatile` 로 escape 시키고, 최종 보증은 링크 단계 장치로 한다.

</details>

## 9. 추가 연습

### 연습 1 — SSO / SBO 경계 측정

할당 없이 담기는 `std::string` 최대 길이와, `std::function` 이 할당하기 시작하는 캡처 크기를 **측정**으로 찾아라.

<details>
<summary>답 보기</summary>

`while (count_allocs([&]{ std::string s(n + 1, 'x'); escape(s); }) == 0) n++;` → 이 맥에서 **22자**. `std::function` 은 `template <size_t N>` 으로 `std::array<uint8_t, N>` 를 캡처해 측정 → 8B=0, 16B=0, 24B=1, 32B=1. 테스트는 이식성을 위해 `sso >= 15`(libstdc++ 15자) 와 "경계+1 에서 정확히 1회"만 단정한다.

</details>

### 연습 2 — 힙 최고 수위(high-water mark)

operator new 가 블록 앞에 16바이트 헤더를 붙여 크기를 기록하게 바꾸고, `live`/`peak` 바이트를 추적하라. `vector<int>` 에 100개를 넣을 때 peak 가 최종 버퍼보다 큰 이유는?

<details>
<summary>답 보기</summary>

재할당 순간 **옛 버퍼와 새 버퍼가 동시에 살아 있기** 때문(복사 후 옛 버퍼 해제). 실측: 최종 버퍼 512B, peak 768B (256 + 512). 펌웨어에서 "용량 N 이면 충분하겠지"라는 계산이 틀리는 지점. 헤더는 16바이트로 둬서 반환 포인터의 16바이트 정렬을 유지한다(정렬 오버로드는 이 연습에서 교체하지 않음).

</details>

### 연습 3 — "초기화 후 힙 잠금"

초기화 단계에서는 할당을 허용하고, `g_locked = true` 이후의 할당만 위반으로 세라. 이미 확보한 vector 원소 쓰기는 괜찮고 `push_back` 으로 용량을 넘기면 위반이 되는지 확인하라.

<details>
<summary>답 보기</summary>

`alloc_hdr` 에서 `if (g_locked) g.violations++;`. `new std::vector<int>(64, 0)` 은 init 이라 허용, `(*table)[5] = 1` 은 할당 없음, `table->push_back(9)` 는 capacity 64 를 넘겨 재할당 → 위반 1. JSF AV C++ Rule 206 ("초기화 이후 free store 할당/해제 금지")을 코드로 강제하는 방식이다.

</details>

### 연습 4 — std::function 대신 함수 포인터 + 컨텍스트

큰 상태(`BigState`, 128B 이상)를 쓰는 콜백을 할당 없이 저장·호출하라. 캡처 없는 람다를 함수 포인터로 쓸 수 있음도 확인하라.

<details>
<summary>답 보기</summary>

`struct Callback { void (*fn)(void*, int); void* ctx; void operator()(int ev) const { if (fn) fn(ctx, ev); } };` — 상태는 호출자가 소유(정적 객체)하고 포인터만 넘긴다. 100번 호출 할당 0. 캡처 없는 람다는 함수 포인터로 암시적 변환된다([expr.prim.lambda.closure]). 이게 C HAL 콜백(`HAL_UART_RegisterCallback`)과 같은 모양이라 extern "C" 경계(CPP-B02)에도 그대로 쓸 수 있다.

</details>

### 연습 5 — 정적 버퍼 위의 std::pmr::vector

`alignas(std::max_align_t) static std::byte g_arena[1024]` 위에 `monotonic_buffer_resource` 를 만들고 upstream 을 `null_memory_resource()` 로 둬서, `std::pmr::vector<int>` 20개 push_back 이 전역 힙을 0회 쓰는지 확인하라.

<details>
<summary>답 보기</summary>

전역 operator new 0회, `v.data()` 가 `g_arena` 안. monotonic 은 해제를 안 하므로 재할당 때 옛 버퍼 공간이 버려진다 → 아레나 크기를 넉넉히. 아레나가 다 차면 upstream(`null_memory_resource`)이 `bad_alloc` 을 던지고, 받을 catch 가 없으니 `std::terminate` → abort 로 끝날 것이다(예상, 실행 안 해 봄) — 즉 "조용한 힙 사용" 대신 "시끄러운 실패". (pmr 상세는 Mid 레벨)

</details>

### 연습 6 — CI 심볼 스캔

빌드 산출물에 힙 관련 심볼이 남아 있는지 `nm` 으로 검사하는 한 줄을 써라. (코드 대신 `build.sh` 마지막 단계에서 실측)

<details>
<summary>답 보기</summary>

```text
$ nm -u fw | c++filt | grep -E "operator new|operator delete|malloc" || echo "(참조 없음)"
(참조 없음)                         ← -nostdlib++ 로 링크한 demo_link_error
$ nm solution | grep -E " __Znwm$| __ZnwmSt11align_val_t$| _malloc$"
0000000100000580 T __Znwm           ← 우리가 정의한 operator new(unsigned long)
0000000100000628 T __ZnwmSt11align_val_t
                 U _malloc
```

펌웨어에서는 `arm-none-eabi-nm -C fw.elf | grep -E "operator new|malloc|_sbrk" && exit 1` 을 CI 게이트로 둔다(`_Znwj` = 32비트 operator new). 맹글링 읽기는 CPP-B02 참고.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ $F practice.cpp -o /tmp/b14_p && /tmp/b14_p
#   SSO capacity = 22 chars, function: 8B=0 16B=0 24B=1 32B=1
#   vector<int> x100: final buffer=512 B, peak live=768 B
# 15/15 passed
bash build.sh        # 하네스 3개 + 데모 3개 + 심볼 스캔 전부
```

## 10. 핵심 정리

- 모든 숨은 할당은 **전역 `::operator new`** 를 지난다. 교체 가능한 함수 20개(new 8 + delete 12)를 정의하면 전부 가로챈다. placement new 는 교체 불가 · 할당 없음.
- 실측(libc++): array 0, string ≤22자 0 / 23자+ 1, vector ×5 = 4 (reserve 시 1), function 캡처 ≤16B 0 / 24B+ 1. 숫자는 구현마다 다르다.
- 측정은 **escape 필수** — `-O2` 는 할당을 지울 수 있다(allocation elision). 카운트 0 ≠ 경로 없음.
- 전역 operator new 는 `= delete` 불가(암시적 선언). 클래스 전용 `= delete` 는 `new T` 만 막고 `std::vector<T>` 는 못 막는다.
- 빌드 단계 차단: 정의 없음(`-nostdlib++` / gcc 드라이버 링크), poison 심볼 + dead strip / gc-sections, `--wrap=malloc` (ARM). 런타임 차단: abort 스텁, `_sbrk → -1`, 힙 0 링커 스크립트.
- virtual 소멸자는 힙을 안 써도 `operator delete` 를 참조한다 → delete 는 트랩으로 정의.
- 0-할당 대안: `std::array`, 고정 링버퍼, 정적 버퍼 + `::new`/`std::construct_at`, 함수 포인터 + ctx, pmr 아레나.

## 참고자료

- [cppreference — operator new, operator new[] (replaceable allocation functions)](https://en.cppreference.com/w/cpp/memory/new/operator_new)
- [cppreference — operator delete, operator delete[]](https://en.cppreference.com/w/cpp/memory/new/operator_delete)
- [cppreference — new expression (allocation elision, class-scope lookup)](https://en.cppreference.com/w/cpp/language/new)
- C++20 표준 (N4861): [replacement.functions], [new.delete.single], [new.delete.array], [new.delete.placement], [basic.stc.dynamic], [dcl.fct.def.delete], [expr.new]
- N3664 "Clarifying Memory Allocation" (C++14 allocation elision), P0035 "Dynamic memory allocation for over-aligned data" (C++17)
- [GNU ld — `--wrap=symbol`, `--gc-sections`, `--cref`](https://sourceware.org/binutils/docs/ld/Options.html)
- [JSF Air Vehicle C++ Coding Standards (Rule 206)](https://www.stroustrup.com/JSF-AV-rules.pdf)
- MISRA C++:2008 Rule 18-4-1 "Dynamic heap memory allocation shall not be used"
- 관련 레슨: C-B11 (메모리 영역), C-A05 (정렬 할당), CPP-B13 (예외/RTTI 끄기), CPP-M07 · M08 · M10 · M12 (정적 할당 기법), CPP-A03 (`__cxa_*` 런타임 심볼)
