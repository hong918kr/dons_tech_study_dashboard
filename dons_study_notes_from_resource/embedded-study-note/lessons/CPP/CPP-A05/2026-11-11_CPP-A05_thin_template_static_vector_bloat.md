---
id: CPP-A05
time: 110분
updated: 2026-11-11
---
# CPP-A05 템플릿 bloat 다이어트: thin template 기법

## 학습 목표

- `static_vector<T,N>` 을 5 타입 × 3 용량(N)으로 인스턴스화했을 때 늘어나는 코드를 **Cortex-M4 오브젝트(clang)와 링크된 ELF(arm-none-eabi-g++)** 에서 함수 단위로 재고, `-ffunction-sections -Wl,--gc-sections` 가 무엇을 지우고 무엇을 못 지우는지 말할 수 있다.
- 템플릿을 **T 도 N 도 모르는 바이트 단위 코어**(`void*` + 원소 크기 + 용량을 런타임 인자로)와 **반드시 인라인되는 얇은 타입 래퍼**로 나누고, "N 이 새 코드를 만들지 않는다"를 심볼 수 0개로 확인한다.
- memcpy 코어를 `MemcpyElem` 개념(concept)으로 trivially copyable T 에만 열고, 비-trivial T 는 **타입별 연산 표(ops table)** 와 **fat 템플릿 유지** 중 무엇이 나은지 실측 숫자로 고른다.
- 리팩터링 전후의 경계·용량 초과 동작이 같다는 것을 xorshift 연산열 **동치 테스트**로 보이고, 단위 테스트가 놓친 버그를 그 테스트가 잡는 것을 확인한다.
- 같은 기계어 함수 합치기(ICF)를 GNU ld(없음) · ld.lld `--icf=all` · 맥 ld64(기본 dedup)에서 관찰하고, 펌웨어에는 무엇이 해당하는지 답한다.

## 1. 문제

"우리 펌웨어 flash 가 꽉 찼는데 `nm --size-sort` 상위가 전부 `static_vector<..., 16>::insert`, `<..., 32>::insert`… 입니다. 어떻게 줄이죠?" — C++ 펌웨어 면접에서 템플릿을 쓰자고 하면 거의 반드시 돌아오는 꼬리질문이다. 정답은 "템플릿을 쓰지 말자"가 아니라 **타입·용량에 의존하지 않는 부분을 한 벌로 빼고, 템플릿은 그 위의 얇은 껍데기로 남기는 것**(thin template, Android/Symbian 시절부터 쓰던 이름)이다. 그리고 숫자로 증명하는 것이다.

이미 배운 것은 다시 하지 않는다: 컨테이너 자체(저장소, 수명, `erase` 의 대입/생성/소멸 구분)는 CPP-M10, 저장소 수명 규칙(`std::launder`, placement new)은 CPP-M07, CRTP 타입 수에 따른 bloat 측정과 `frame_core` 끌어올리기는 CPP-M05, 빌드 시간 비용은 MCPP-A09 가 담당한다. 이 레슨은 그 위에서 **"코드 크기를 무엇이 결정하나"를 측정하고, 측정이 시키는 대로 설계를 바꾼다.**

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">fat: (T, N) 마다 멤버 함수 한 벌  vs  thin: 코어 한 벌 + 녹아 없어지는 래퍼</text>
  <text x="10" y="48" fill="currentColor">fat::static_vector — 5 타입 × 3 N = 15 벌</text>
  <g stroke="currentColor">
    <rect x="10" y="58" width="68" height="34" class="f-warn"/><rect x="82" y="58" width="68" height="34" class="f-warn"/><rect x="154" y="58" width="68" height="34" class="f-warn"/>
    <rect x="10" y="96" width="68" height="34" class="f-warn"/><rect x="82" y="96" width="68" height="34" class="f-warn"/><rect x="154" y="96" width="68" height="34" class="f-warn"/>
    <rect x="10" y="134" width="68" height="34" class="f-warn"/><rect x="82" y="134" width="68" height="34" class="f-warn"/><rect x="154" y="134" width="68" height="34" class="f-warn"/>
    <rect x="10" y="172" width="68" height="34" class="f-warn"/><rect x="82" y="172" width="68" height="34" class="f-warn"/><rect x="154" y="172" width="68" height="34" class="f-warn"/>
    <rect x="10" y="210" width="68" height="34" class="f-warn"/><rect x="82" y="210" width="68" height="34" class="f-warn"/><rect x="154" y="210" width="68" height="34" class="f-warn"/>
  </g>
  <g fill="currentColor" font-size="12" font-family="monospace">
    <text x="16" y="80">u8,4</text><text x="88" y="80">u8,16</text><text x="160" y="80">u8,64</text>
    <text x="16" y="118">u16,4</text><text x="88" y="118">u16,16</text><text x="160" y="118">u16,64</text>
    <text x="16" y="156">u32,4</text><text x="88" y="156">u32,16</text><text x="160" y="156">u32,64</text>
    <text x="16" y="194">i32,4</text><text x="88" y="194">i32,16</text><text x="160" y="194">i32,64</text>
    <text x="16" y="232">CAN,4</text><text x="88" y="232">CAN,16</text><text x="160" y="232">CAN,64</text>
  </g>
  <text x="10" y="268" fill="currentColor" font-size="12">각 칸 = push/insert/erase/... (인라인되면 호출처마다 또 복제)</text>
  <text x="380" y="48" fill="currentColor">thin — 코어는 T·N 을 런타임 인자로 받는다</text>
  <rect x="380" y="58" width="350" height="70" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="392" y="80" fill="currentColor" font-family="monospace" font-size="12">push_raw(size&amp;, buf, geom{esz,cap}, src)</text>
  <text x="392" y="98" fill="currentColor" font-family="monospace" font-size="12">insert_raw / erase_raw / pop_raw / at_raw</text>
  <text x="392" y="118" fill="currentColor" font-size="12">비템플릿 · 전체에 1 벌 (gcc -Os 링크 후 202 B)</text>
  <rect x="380" y="150" width="350" height="56" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="392" y="172" fill="currentColor" font-family="monospace" font-size="12">static_vector&lt;T,N&gt;::push_back(v)</text>
  <text x="392" y="192" fill="currentColor" font-family="monospace" font-size="12">→ push_raw(size_, storage_, {esz,N}, &amp;v)</text>
  <text x="392" y="232" fill="currentColor" font-size="12">[[gnu::always_inline]] → 호출처에 "인자 준비 + bl" 만 남는다</text>
  <text x="392" y="252" fill="currentColor" font-size="12">static_vector&lt;..&gt; 심볼: fat 15 / thin 0 (실측)</text>
  <line x1="555" y1="150" x2="555" y2="130" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="560" y="144" fill="currentColor" font-size="12">bl</text>
</svg>
```

### Part A — 바이트 단위 비템플릿 코어 (trivially copyable T)

- `push_raw / pop_raw / insert_raw / erase_raw / at_raw` — 상태는 `(std::uint16_t& size, std::byte* buf)`, 모양은 `geom{esz, cap}` 로 받는다. 계약은 주어진 `fat::static_vector` 와 **똑같다**: 꽉 차면 `false`, `insert(i)` 는 `i > size` 면 `false`, `erase(i)`/`at(i)` 는 `i >= size` 면 실패. `N == 0` 도 컴파일되고 항상 실패.
- 래퍼(`vec_ref<T>`, `static_vector<T,N>`)는 주어진다. 객체 레이아웃은 fat 과 같은 `{size, 저장소}` — 버퍼 포인터를 객체에 저장하지 않는 이유는 5. 풀이 "전역 배치"에서 실측으로 본다.

### Part B — 어려운 부분: 별칭, 비-trivial T, 동치

- `v.insert(0, v[2])` — 인자가 **밀려나는 구간 안의 자기 원소**를 가리키면 memmove 뒤에 원본이 한 칸 뒤에 있다. 코어가 알아서 고쳐 읽어야 한다.
- 비-trivial T(카운터 달린 `Tracked`)용 **ops 코어** `push_ops / pop_ops / insert_ops / erase_ops / clear_ops`: 원소마다 `elem_ops` 표의 함수 포인터로 생성·대입·소멸. 살아 있는 주소 장부가 이중 생성/죽은 칸 대입을 잡는다.
- fat 과 thin 에 **같은 xorshift 연산열**(push/pop/insert/erase/별칭 insert/clear/at, 범위 밖 인덱스 포함)을 주고 매 단계 반환값과 내용이 같은지 본다.

### Part C — CAN 게이트웨이: N 이 다른 큐를 함수 하나로

- `drop_ids(vec_ref<CanFrame> q, lo, hi)` — CAN1(16칸)·CAN2(4칸) 수신 큐에서 OBD 진단 id(0x7DF–0x7EF)를 순서 유지하며 지운다. 함수는 **하나**다(N 무관).
- `log_push_evict(vec_ref<LogRec>, rec, evicted&)` — 로그가 꽉 차면 가장 오래된 것을 버리고 넣는다.
- 텔레메트리: `q.bytes()` 의 CRC-32 가 기대 프레임 배열의 CRC 와 같아야 한다. `bytes()` 는 패딩 없는 T 에만 컴파일된다.

## 2. Problem (English)

> **Shrink a fixed-capacity vector that is instantiated for many (T, N) pairs.**
>
> Given `fat::static_vector<T, N>` (`push_back`, `pop_back`, `insert(i, v)`, `erase(i)`, `at(i)`, `clear`; every failure returns `false`/`nullptr` and leaves the container unchanged), firmware instantiates it for 5 element types × 3 capacities.
>
> 1. Measure `.text` per function on a Cortex-M4 object and on a linked ELF, with and without `-ffunction-sections -Wl,--gc-sections`.
> 2. Refactor into a non-template core that works on bytes — `bool insert_raw(uint16_t& size, std::byte* buf, geom g, size_t i, const void* src)` etc., where `geom = {uint16_t esz, uint16_t cap}` — plus a thin typed wrapper. The capacity `N` must not cause any new function to be emitted. Restrict the memcpy core to trivially copyable `T` with a concept; for other `T` either use a per-type table of operations or keep the full template, and justify the choice with numbers.
> 3. Behaviour must be identical before and after: prove it with an equivalence test over random operation sequences, including out-of-range indices, full containers, and inserting a reference to the container's own element.
> 4. Report before/after sizes and where identical-code folding (ICF) would or would not help on your toolchain.
>
> Constraints: C++20, `-fno-exceptions -fno-rtti`, no heap, capacity ≤ 65535.
>
> Follow-ups: What does it cost at each call site? Why not store a buffer pointer in the object? Does ICF make this unnecessary? What about `-flto`? What breaks for a type with padding?

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 대비
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
```

처음 실행 결과 (실제 출력, PASS 줄과 일부 FAIL 생략): 경고 0, `8/48 passed`, exit 1.

```text
FAIL starter.cpp:429  v.push_back(10) && v.push_back(20) && v.push_back(30)
FAIL starter.cpp:460  insert_own(v, 0, 2) && v.size() == 5
FAIL starter.cpp:471  t.push_back(a) && t.push_back(b) && t.push_back(c)
  mismatch at step 7 (op 2, i 1)
FAIL starter.cpp:490  ok
  mismatch at step 0 (op 3, i 1)
FAIL starter.cpp:510  drop_ids(can1, 0x7DF, 0x7EF) == 3 && can1.size() == 3
8/48 passed
```

스텁은 "아무것도 안 하고 실패를 돌려준다" 라서 멈추거나 죽지 않는다. 테스트는 원소를 `K(v, i)` (없으면 `0xFFFFFFFF`) 로만 읽어서 스텁 상태에서도 널 역참조가 없다. 컴파일 시점 증명(`static_assert`)은 처음부터 통과한다 — 그건 주어진 래퍼의 성질이다.

관찰용 파일 (하네스와 별개, 결과는 5. 풀이에 실제 출력으로):

| 파일 | 내용 |
|---|---|
| `demo_vec.hpp`, `demo_thin_core.cpp` | solution.cpp 의 fat·thin 을 그대로 옮긴 헤더 + 코어 정의(별도 번역 단위) |
| `demo_bloat.cpp` | 5 타입 × N(4,16,64) 가짜 펌웨어. `-DIMPL=0/1`(fat/thin) `-DNS=3/1` `-DSITES=1/3`(연산당 호출처 수) `-DNONTRIV=1` `-DKEEP_MEM` |
| `demo_global_placement.cpp` | 버퍼 포인터를 저장하는 설계의 전역 객체가 어디 놓이나 (+ `-DSHOW_ERROR`) |
| `demo_errors.cpp` | 래퍼가 막는 것 5가지 (`-DSHOW_ERROR`) |
| `bloat.sh` | `obj`(맥, clang thumbv7em) · `place`(맥) · `host`(맥 ld64) · `link`(VM, arm-none-eabi-g++ 14.2.1 + ld.lld 21) |

```bash
bash bloat.sh obj; bash bloat.sh place; bash bloat.sh host     # 맥 (ARM_INC 없으면 VM 에서 헤더를 가져온다)
limactl shell elinux -- bash -c 'mkdir -p /tmp/CPP-A05 && cp <lesson dir>/* /tmp/CPP-A05/ && bash /tmp/CPP-A05/bloat.sh link'
```

## 4. 힌트

<details>
<summary>힌트 1 — 템플릿 인자가 코드의 어디에 쓰이는지 적어 보라</summary>

`T` 는 `sizeof(T)` (주소 계산, 복사 길이)와 복사/소멸 방법으로만 쓰인다. `N` 은 `size >= N` 비교 한 번뿐이다. 둘 다 **값**으로 바꿀 수 있다: `esz = sizeof(T)`, `cap = N`. 복사 방법은 trivially copyable 이면 `memcpy` 하나로 통일된다. 남는 템플릿 코드는 "상수를 인자로 넣고 코어를 부르는" 한 줄뿐이다.

</details>

<details>
<summary>힌트 2 — 별칭 insert: 밀고 나서 어디를 읽나</summary>

`memmove(p + esz, p, (size - i) * esz)` 로 `[i, size)` 가 한 칸 뒤로 간다. `src` 가 그 구간 안이었다면 원본은 이제 `src + esz` 에 있다. 구간 판정은 `std::less<const std::byte*>` 로 — 서로 다른 배열의 포인터에 내장 `<` 는 결과가 unspecified 다 ([expr.rel]). `std::less` 는 구현이 정한 전순서를 보장한다 ([comparisons.general]).

</details>

<details>
<summary>힌트 3 — ops 코어는 fat 의 erase/insert 를 함수 포인터로 옮겨 적기</summary>

insert: 끝의 raw 칸에 `move_construct(끝, 끝-1)`, 그다음 뒤에서부터 `move_assign(k, k-1)`, 마지막에 `copy_assign(i, src)`. erase: 앞으로 `move_assign(k, k+1)` 후 마지막 칸 `destroy`. raw 칸에는 생성만, 살아 있는 칸에는 대입만 (CPP-M10 의 불변식 그대로).

</details>

<details>
<summary>힌트 4 — drop_ids 의 인덱스</summary>

지웠으면 `i` 를 늘리지 않는다 (뒤가 당겨져 왔으니까). 이건 O(n²) 이다 — 한 번에 압축하는 판은 연습 2.

</details>

## 5. 풀이

### Part A — 무엇을 런타임 값으로 내리나

| 템플릿 인자가 하던 일 | thin 에서 | 비용 |
|---|---|---|
| `sizeof(T)` — 주소·복사 길이 | `geom.esz` (호출처 상수) | 코어 안에서 곱셈 `i * esz` (M4: `mul`/`mla` 1명령) |
| `N` — 용량 비교 | `geom.cap` (호출처 상수) | 없음 (비교는 원래 있었다) |
| T 의 복사 생성 / 대입 / 소멸 | trivially copyable → `memcpy`/`memmove`, 아니면 `elem_ops` 표 | memcpy 는 libc 호출 (fixed cost 아래), ops 는 원소마다 간접 호출 |
| 객체 안의 위치 (`size_`, `buf_`) | 래퍼가 `(size_, storage_)` 를 인자로 넘김 | 인자 레지스터 2개 |

```cpp
bool insert_raw(std::uint16_t& size, std::byte* buf, geom g, std::size_t i, const void* src) noexcept {
    if (size >= g.cap || i > size) return false;
    const std::byte* s = static_cast<const std::byte*>(src);
    if (in_range(s, buf, i, size, g.esz)) s += g.esz;      // Part B: 밀린 자기 원소
    std::byte* p = addr(buf, i, g.esz);
    std::memmove(p + g.esz, p, (size - i) * g.esz);
    std::memcpy(p, s, g.esz);
    ++size;
    return true;
}
```

memcpy 로 원소를 "생성"해도 되는 근거: C++20 부터 `memcpy`/`memmove` 는 대상 영역에 객체를 **암시적으로 생성**한다 ([cstring.syn], [intro.object] — P0593). 그래도 꺼낼 때는 `std::launder` 를 거친다(CPP-M07). 래퍼의 `at()` 이 그렇게 한다.

`geom` 을 4바이트 구조체로 묶은 이유는 실제 asm 에 보인다 (clang 21, thumbv7em, -Os, `prod_u32_16_1` 일부 — `bash bloat.sh obj`):

```text
3f6:  adds  r5, r4, #0x4     ; storage_ = &g_u32_16 + 4   (r4 = &g_u32_16 = &size_)
3f8:  movs  r2, #0x4         ; geom.esz = 4
3fa:  str   r0, [sp, #0xc]   ; 넣을 값을 스택 임시에
3fc:  movt  r2, #0x10        ; geom.cap = 16  → r2 = 0x0010_0004, 레지스터 하나 (AAPCS: 4바이트 이하 복합 타입)
400:  add   r3, sp, #0xc     ; src
402:  mov   r0, r4           ; &size_
404:  mov   r1, r5           ; storage_
406:  bl    0x406 <prod_u32_16_1+0x20>   ; push_raw (링크 전 오브젝트라 재배치 자리)
```

(`;` 뒤 주석은 내가 붙인 것이다.)

호출처마다 남는 것은 이 "인자 준비 + `bl`" 이다. 함수 크기로 비교하면 (u32 인스턴스, clang -Os 오브젝트):

| | fat (N=4/16/64 각각) | thin (N=4/16/64 각각) |
|---|---|---|
| 생산자 `prod_u32_N_1` | 0x3a = 58 B | 0x44 / 0x48 / 0x48 |
| 소비자 `cons_u32_N_1` | 0x6a = 106 B | 0x42 = 66 B |
| 컨테이너 멤버 | `insert` 0x62 = 98 B **× 3** | 없음 (코어 공유) |
| (T,N) 하나당 합 | 262 B | 134–138 B + 공유 코어 |

### 전역 배치 — 버퍼 포인터를 객체에 넣으면 생기는 일

ETL 의 `etl::vector<T, MAX_SIZE>` 는 `ivector<T>(reinterpret_cast<T*>(&buffer), MAX_SIZE)` 로 생성하고 `ivector` 가 `p_buffer`/`p_end` 를 멤버로 갖는다. LLVM `SmallVectorBase` 도 `void* BeginX; Size, Capacity` 를 갖는다 (둘 다 소스 확인). "N 을 지운 `ivector<T>&` 하나로 모든 용량을 받는다" 는 장점이 있다. 그런데 전역으로 두면? 같은 64 × u32 벡터를 네 설계로 만들었다 (`bash bloat.sh place`, VM 쪽은 `link` 의 마지막 부분, 실측):

| 전역 | clang -Os/-O2 | clang -O0 | gcc 14 -Os/-O2 |
|---|---|---|---|
| `fat::static_vector` {size, 저장소} | `.bss` 260 B | `.bss` | `.bss` (+ 공용 init 함수) |
| `thin::static_vector` (이 레슨) {size, 저장소} | `.bss` 260 B | `.bss` | `.bss` |
| `PtrVec` — 생성자에서 `buf_ = storage_` | **`.data` 264 B** | `.bss` + `__cxx_global_var_init` | `.bss` + `_GLOBAL__sub_I` (`.init_array`) |
| `PtrVecC` — constexpr 생성자 + `storage_{}` (constinit) | **`.data` 264 B** | `.data` | **`.data`** |

`.data` 는 **flash 에 초기값 이미지 + RAM 에 사본** 이다. 포인터 4바이트 때문에 0 이 아닌 객체가 되어, 256바이트 저장소까지 통째로 flash 에 실린다 — 코드 bloat 을 줄이려다 데이터 bloat 을 만든 셈이다. clang 은 동적 초기화를 정적 데이터로 접어서(constant folding) `.data` 로, gcc 는 접지 않고 `.init_array` 로 보냈다. `.init_array` 쪽은 flash 는 아끼지만 초기화 순서 문제가 생긴다(CPP-B09: 부팅 중 그 벡터를 먼저 쓰는 코드가 있으면 나중에 덮인다). 저장소를 초기화하지 않고 `constinit` 을 요구하면 둘 다 컴파일 에러다 (`-DSHOW_ERROR`, 실측):

```text
demo_global_placement.cpp:41:36: note: non-constexpr constructor 'PtrVec' cannot be used in a constant expression
demo_global_placement.cpp:47:38: note: subobject 'storage_' is not initialized
```

그래서 이 레슨의 thin 은 객체에 **아무 포인터도 저장하지 않는다**: 객체 = fat 과 같은 `{uint16_t size_; alignas(T) std::byte storage_[...]}` (`sizeof` 같음을 `static_assert` 로 확인). N 을 지운 접근은 `std::span` 처럼 **값으로 만드는 뷰** `vec_ref<T>{&size_, storage_, N}` 로 한다. 대가: 뷰는 3워드이고, span 처럼 원본보다 오래 살면 매달린다(dangling).

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">객체에 포인터를 넣느냐 마느냐 → 전역이 놓이는 곳 (clang -Os, thumbv7em, 실측)</text>
  <text x="10" y="50" fill="currentColor">ETL/LLVM 식: {buf_*, size, cap, storage[256]}</text>
  <rect x="10" y="60" width="40" height="34" class="f-warn" stroke="currentColor"/>
  <text x="16" y="82" fill="currentColor" font-size="12">buf_</text>
  <rect x="50" y="60" width="300" height="34" class="f-warn" stroke="currentColor"/>
  <text x="60" y="82" fill="currentColor" font-size="12">size cap + storage (0 이어도 함께)</text>
  <text x="10" y="114" fill="currentColor" font-size="12">→ .data 264 B (flash 이미지 + RAM 사본)</text>
  <text x="400" y="50" fill="currentColor">이 레슨: {size, storage[256]} + 뷰</text>
  <rect x="400" y="60" width="40" height="34" class="f-ok" stroke="currentColor"/>
  <text x="404" y="82" fill="currentColor" font-size="12">size</text>
  <rect x="440" y="60" width="290" height="34" class="f-ok" stroke="currentColor"/>
  <text x="450" y="82" fill="currentColor" font-size="12">storage (전부 0) → .bss 260 B</text>
  <text x="400" y="114" fill="currentColor" font-size="12">flash 0 B, 부팅 때 0 채우기만</text>
  <rect x="400" y="140" width="330" height="60" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="410" y="162" fill="currentColor" font-family="monospace" font-size="12">vec_ref&lt;T&gt;{&amp;size_, storage_, N}</text>
  <text x="410" y="182" fill="currentColor" font-size="12">필요할 때 만드는 값 (레지스터 3개), 저장 안 함</text>
  <line x1="420" y1="94" x2="420" y2="140" stroke="currentColor" stroke-dasharray="4 3"/>
  <line x1="580" y1="94" x2="580" y2="140" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="10" y="154" fill="currentColor" font-size="12">gcc 14: 같은 PtrVec 을 .bss + .init_array 로</text>
  <text x="10" y="172" fill="currentColor" font-size="12">→ flash 는 아끼지만 초기화 순서 위험 (CPP-B09)</text>
  <text x="10" y="190" fill="currentColor" font-size="12">constinit 판은 두 컴파일러 모두 .data</text>
  <text x="10" y="230" fill="currentColor" font-size="12">trade-off: 뷰는 span 처럼 원본보다 오래 살면 dangling. ivector&amp; 는 진짜 객체 참조라 그 문제가 없다.</text>
</svg>
```

### 측정 1 — clang 21 thumbv7em 오브젝트 (레시피 b, `bash bloat.sh obj`)

`.text` 바이트 (llvm-size). 이름 = 구현-N 종류 수-연산당 호출처 수. 3N = 15 인스턴스, 1N = 5 인스턴스 (N=16 만).

| 설정 | fat -Os | thin -Os | fat -O2 | thin -O2 |
|---|---|---|---|---|
| 3N-1S | 4622 | **3230** (−30%) | 8042 | **3338** (−58%) |
| 1N-1S | 1558 | 1526 | 2698 | 1570 |
| 3N-3S | 10550 | **8350** (−21%) | 24014 | **8642** (−64%) |
| 1N-3S | 3534 | 3234 | 8022 | 3338 |

- fat 의 "컨테이너 코드"(prod_/cons_ 밖의 함수, `llvm-nm -S` 합)는 -Os 에서 1N 544 → 3N 1632 B, 정확히 3배. thin 의 코어는 1N 이든 3N 이든 **648 B** 그대로이고, `static_vector<` 가 이름에 들어간 함수 심볼은 fat 15 개, thin 0 개.
- -O2 에서 fat 은 멤버를 전부 호출처에 인라인한다 (`lib 0`, `app 7746`). 그래서 bloat 이 컨테이너 심볼이 아니라 **사용자 함수 크기**로 숨는다 — `nm` 상위에 `static_vector` 가 안 보인다고 bloat 이 없는 게 아니다.
- 이 오브젝트 숫자는 링크 전이라 thin 코어 TU 의 안 쓰는 `*_ops` 함수(약 380 B)도 들어 있다. 그런데도 3N 에서 이긴다.
- `-DTHIN_CORE_INLINE` (코어까지 헤더에 `inline`): -Os 3766, -O2 3830 — 코어가 호출처에 인라인되면 이득이 줄어든다. 코어는 **별도 .cpp** 에 둔다 (또는 `[[gnu::noinline]]`). LTO 를 켜면 다시 인라인될 수 있다 — 측정하지 않았다(미확인).

### 측정 2 — arm-none-eabi-g++ 14.2.1 링크 ELF (레시피 c, `bloat.sh link`)

`.text` 바이트. `gc` = `-ffunction-sections -fdata-sections -Wl,--gc-sections`. `mem` = `-DKEEP_MEM` (`_start` 가 memcpy/memmove 를 붙잡음 — "이미 다른 코드가 memcpy 를 쓰는 펌웨어" 기준선). `icf` = `-fuse-ld=lld -Wl,--icf=all`.

| 설정 | Os | Os+gc | Os+gc+mem | O2+gc+mem | Os+gc+mem+icf |
|---|---|---|---|---|---|
| fat-3N-1S | 4256 | 3896 | 4524 | 5100 | 4524 |
| thin-3N-1S | 3864 | 3488 | **3504** | **3596** | 3504 |
| fat-1N-1S | 1392 | 1312 | 1940 | 2132 | 1940 |
| thin-1N-1S | 2060 | 1720 | 1736 | 1780 | 1736 |
| fat-3N-3S | 8538 | 8264 | 8892 | 13768 | 8060 |
| thin-3N-3S | 8456 | 7876 | **7892** | **8784** | 7892 |
| fat-1N-3S | 3052 | 3004 | 3632 | 5004 | 3460 |
| thin-1N-3S | 3792 | 3408 | 3424 | 3508 | 3424 |

이 표는 이야기를 그대로 따라주지 않는다. 읽는 법:

1. **고정비가 있다.** fat 은 gcc 가 원소 루프를 인라인해서 memcpy/memmove 를 **안 부른다**. thin 은 부른다 → newlib `memcpy` 0x134 + `memmove` 0x130 = **612 B** 가 링크된다. 그래서 인스턴스 5개(1N)에 memcpy 가 없는 펌웨어라면 thin 이 **더 크다** (1720 vs 1312). 실제 펌웨어는 거의 항상 memcpy 를 이미 링크하고 있다(구조체 복사, HAL, 스타트업) — `mem` 열이 그 경우이고, 그러면 1N 에서도 thin 이 작다 (1736 vs 1940).
2. **손익분기.** memcpy 가 없다면 이 데모에선 (T,N) 인스턴스 5개는 fat, 15개는 thin 이 이긴다 (Os+gc). 숫자는 데모에 묶인 것이고, 일반 규칙은 "고정비(코어 ≈ 200 B + memcpy/memmove) vs (T,N) 당 멤버 본문" 이다.
3. **-O2 에서 차이가 크다** (−30%, −36%). fat 은 -O2 에서 호출처마다 인라인 복제된다.
4. **gc-sections 가 thin 에 더 중요하다.** 비템플릿 코어는 `-ffunction-sections` 없이는 한 `.text` 섹션이라 안 쓰는 `*_ops` 함수 5개가 남는다 (실측 thin-3N-1S, Os, KEEP_MEM: `--gc-sections` 만 → 3872 B, ops 함수 5개 / `-ffunction-sections` 추가 → 3540 B, 0개 / `-fdata-sections` 까지 → 3504 B). 템플릿 인스턴스는 원래 COMDAT 그룹이라 함수마다 섹션이 따로 생긴다.
5. **함수 합** (Os+gc+mem, nm): fat-3N-3S 는 컨테이너 함수 2336 B (호출처가 여럿이면 gcc 도 멤버를 뺀다), thin-3N-3S 는 240 B (코어 202 + `mk<CanFrame>` 38).

**always_inline 이 필요했던 이유 (실측으로 설계를 바꾼 지점).** 처음 판의 래퍼는 그냥 한 줄짜리 inline 이었다. gcc -Os 에서 호출처가 3개가 되자 `thin::static_vector<CanFrame, 16u>::push_back` 같은 **(T,N) 별 forwarder 를 함수로 빼냈다** — `static_vector<T,N>::…` 75개(15 인스턴스 × at/erase/insert/pop_back/push_back) + `vec_ref<T>::…` 10개, `static_vector` 쪽만 합 1364 B = (T,N) 당 약 91 B. "N 은 코드를 만들지 않는다" 가 설계가 아니라 **인라이너 판단**에 걸려 있던 것이다. 래퍼에 `[[gnu::always_inline]]` 을 붙이자 래퍼 심볼 0개, thin-3N-1S 4044 → 3504, thin-3N-3S 8112 → 7892 (Os+gc+mem). 대신 1N-3S 는 3344 → 3424 로 80 B 늘었다 (호출처마다 인자 준비가 펼쳐진 만큼). clang 숫자는 그대로였다 (원래 인라인하고 있었다).

### Part B — 별칭, ops 표, 동치

**별칭 insert.** `v = {11,22,33,44}` 에서 `v.insert(0, v[2])` 는 memmove 후 `33` 이 `v[3]` 자리로 간다. `src` 를 그대로 읽으면 `22` 를 넣는다. 코어는 `src ∈ [i, size)` 이면 `src += esz`. LLVM `SmallVector::insert_one_impl` 도 같은 일을 한다: `if (!TakesParamByValue && this->isReferenceToRange(EltPtr, I, this->end())) ++EltPtr;` (소스 확인). fat 판은 `T tmp(v)` 로 먼저 복사해서 피했다 — 바이트 코어는 `T` 를 모르니 임시를 만들 수 없어서 포인터를 고친다. `src` 가 `i` 보다 앞이면 움직이지 않으니 그대로 (테스트 `insert_own(v, 4, 1)`).

**비-trivial T: ops 표 vs fat 유지.** 타입마다 생기는 것 (clang -Os, `Counted<1>`, 실측):

```text
.rodata 00000014 thin::ops_for<Counted<1>>             ← 함수 포인터 5개 = 20 B, flash 상수
.text   00000014 void thin::op_copy_construct<Counted<1>>(void*, void const*)
.text   00000014 void thin::op_move_construct<Counted<1>>(void*, void*)
.text   00000014 void thin::op_copy_assign<Counted<1>>(void*, void const*)
.text   00000014 void thin::op_move_assign<Counted<1>>(void*, void*)
.text   00000010 void thin::op_destroy<Counted<1>>(void*)
```

N 과 무관하게 타입당 ~100 B + 표 20 B. 크기 (3 타입 × N 4/16/64):

| | clang -Os 오브젝트 | clang -O2 오브젝트 | gcc Os+gc+mem | gcc O2+gc+mem |
|---|---|---|---|---|
| fat (전체 템플릿) | 4454 | 7496 | 4556 | 4720 |
| thin ops 표 | 3326 | 3356 | 3616 | 3740 |

크기는 ops 표가 이긴다. 대가는 속도다: `erase_ops` 의 루프는 원소마다 표에서 함수 포인터를 읽고 간접 호출한다 (clang -Os, 실측 일부):

```text
230: ldr.w  r2, [r11, #0xc]     ; op.move_assign
234: add.w  r0, r8, r4
238: add.w  r1, r8, r10
23c: blx    r2                  ; 원소마다 간접 호출 (인라인 불가)
...
246: blo    0x230
```

fat 은 `Counted` 의 대입을 루프 안에 인라인한다. 사이클은 재지 않았다 (맥에 DWT 없음 — 명령 수준 추론만): 원소당 `ldr + blx + 함수 prologue/epilogue + bx lr` 가 더해진다. 결정 기준: **비-trivial 원소 컨테이너가 핫 패스(ISR, 매 tick)면 fat, 설정·관리 경로면 ops 표.** 연습 1 의 `best_vector<T,N>` 은 "trivially copyable 이면 thin, 아니면 fat" 이라는 보수적인 선택을 타입으로 굳힌다.

**`MemcpyElem` 이 왜 `is_trivially_copyable` 만이 아닌가.** 복사 생성자를 `delete` 하고 이동 생성자를 `= default` 한 `MoveOnlyPod` 는 `is_trivially_copyable_v == true` 다 (`static_assert` 로 확인). `push_back(const T&)` 를 memcpy 로 하면 **금지된 복사를 몰래 한다.** 그래서 `is_trivially_copy_constructible` 과 trivially destructible 을 함께 요구하고, 래퍼 자체는 `std::copyable<T>` 로 막는다 (에러 ⑤ 아래).

**동치 테스트 결과** (solution 실제 출력):

```text
  trivial: steps=61670 push_full=6397 insert_bad=5409 erase_bad=4272 alias=3522
  Tracked: steps=19937 push_full=2067 alias=1206 ledger bad=0 live=0
```

u8/N=4, u32/N=16, CanFrame/N=7, u32/N=0 과 Tracked/N=6 (ops 경로) 에서 매 단계 반환값·내용이 같았다. 용량 초과 6397번, 범위 밖 insert 5409번, 별칭 insert 3522번을 지나갔다. **단위 테스트만으로는 부족했다:** solution 사본에 버그 4개를 심어 보니 (1) 별칭 보정 삭제 (2) `pop_raw` 빈 검사 삭제 (3) `erase_ops` 가 엉뚱한 칸 소멸 은 단위 CHECK 도 잡았지만, (4) `insert_raw` 범위 검사를 `i > size + 1` 로 한 off-by-one 은 **동치 테스트만** 잡았다 (단위 테스트의 범위 밖 인덱스가 우연히 둘 다 넘었다).

### ICF — 같은 기계어 함수 합치기는 무엇을 해 주나

| 링커 | 관찰 (실측) | 펌웨어 해당? |
|---|---|---|
| GNU ld (arm-none-eabi 2.x, 이 패키지) | `--help` 에 icf 없음 (0줄). gold 는 이 패키지에 없다 | **대부분의 GCC 펌웨어**: ICF 없음 |
| GCC `-fipa-icf` (컴파일러 안, -Os/-O2 기본) | fat-3N-1S: 기본 3896 = `-fno-ipa-icf` 3896, 차이 없음 | 같은 TU 안에서만, 이 데모엔 효과 0 |
| ld.lld 21 `--icf=all` | fat-3N-3S 8892 → 8060. `-fno-inline` fat: 7164 → 5008. 접힌 것: `erase<u32,4/16/64>`, `erase<i32,…>` 6개 → 1개, `push_back<u32,16>` ↔ `<i32,16>` | LLVM 툴체인(clang + lld) 쓰는 펌웨어 |
| 맥 ld64 (기본 dedup) | fat -Os: 6904 → 6520. `-fno-inline`: 13520 → 9232, `erase<int/unsigned, 4/16/64>` 6개가 같은 주소 `0x100001650` | 호스트 테스트 바이너리뿐 |

결론: ICF 는 **바이트가 같은 함수**만 접는다. N 이 비교 상수로 들어간 `push_back`/`insert` 는 N 마다 다르고, 인라인되어 사용자 함수에 녹은 코드는 대상이 아니다. `erase` 처럼 N 을 안 쓰는 멤버, 그리고 `uint32_t`/`int32_t` 처럼 같은 크기 타입끼리만 접힌다. thin 은 애초에 접을 게 없다 (thin-3N-1S 3504 → 3504). 그리고 `--icf=all` 은 서로 다른 함수의 주소가 같아질 수 있다 (함수 포인터 비교에 의존하는 코드가 깨짐) — clang 의 address-significance 표를 쓰는 `--icf=safe` 가 있다. GCC 오브젝트에서 safe 가 얼마나 접는지는 확인하지 않았다(미확인).

### Part C — N 을 지운 함수 하나

```cpp
static std::size_t drop_ids(thin::vec_ref<CanFrame> q, std::uint32_t lo, std::uint32_t hi) {
    std::size_t dropped = 0;
    for (std::size_t i = 0; i < q.size();) {
        if (q[i].id >= lo && q[i].id <= hi) { q.erase(i); ++dropped; }
        else ++i;
    }
    return dropped;
}
```

`drop_ids(can1, …)` (16칸) 과 `drop_ids(can2, …)` (4칸) 이 같은 함수를 부른다 — `static_vector<T,N>` 이 `vec_ref<T>` 로 암시적 변환된다 (배열 → span 처럼). fat 이라면 `template <std::size_t N> drop_ids(fat::static_vector<CanFrame,N>&)` 가 되어 **N 마다 사용자 함수까지** 복제됐을 것이다. 이게 thin 의 두 번째 이득이다: 컨테이너만이 아니라 **컨테이너를 받는 코드**도 N 에서 자유로워진다.

`bytes()` 는 `MemcpyElem<T> && std::has_unique_object_representations_v<T>` 일 때만 있다. `CanFrame` 은 `pad[3]` 을 명시해서 16바이트 전부가 값이고, `CanFrameLoose` 는 같은 16바이트 중 3바이트가 컴파일러 패딩이다. 패딩 바이트의 값은 정해지지 않으니 "같은 프레임 → 같은 CRC" 가 깨진다. 텔레메트리 CRC 테스트는 `crc32(can1.bytes()) == crc32(as_bytes(expect))` 로 통과한다 (`expect` 는 같은 `CanFrame` 배열, `{}` 로 pad 0).

**컴파일 에러로 막는 것** (`demo_errors.cpp -DSHOW_ERROR`, Apple clang 21 실제 출력 발췌):

```text
demo_errors.cpp:26:17: error: invalid reference to function 'bytes': constraints not satisfied          ① 패딩 있는 T
demo_errors.cpp:13:13: note: candidate function not viable: no known conversion from
      'thin::static_vector<CanFrameLoose, 4>' to 'thin::vec_ref<CanFrame>' for 1st argument             ② 다른 T
./demo_vec.hpp:176:19: error: static assertion failed due to requirement '70000UL <= 65535': capacity must fit uint16_t   ③
demo_errors.cpp:31:10: error: call to deleted constructor of 'thin::static_vector<CanFrame, 16>'       ④ 복사 금지
demo_errors.cpp:33:11: error: constraints not satisfied for class template 'static_vector' [with T = MoveOnlyPod, N = 4]
./demo_vec.hpp:174:11: note: because 'MoveOnlyPod' does not satisfy 'copyable'                          ⑤
```

⑤ 는 처음엔 `std::copyable` 제약이 없어서 `ops_for<MoveOnlyPod>` 를 인스턴스화하는 깊은 곳에서 `construct_at`·복사 대입·이동 대입 에러가 줄줄이 나왔다 — 클래스 템플릿 머리에 개념을 둬서 한 줄 에러로 바꿨다. 하네스는 같은 것을 개념 `HasBytes<V>` 와 `static_assert` 로 증명한다. requires-식은 템플릿(개념) 밖에서 쓰면 "거짓" 이 아니라 그냥 컴파일 에러다 ([expr.prim.req]) — 처음 판에서 실제로 그 에러가 났다.

### 정리: 언제 무엇을

| 상황 | 선택 | 근거 (이 레슨 실측) |
|---|---|---|
| 같은 컨테이너를 (T,N) 여러 개로, trivially copyable T | thin (코어 .cpp + always_inline 래퍼) | -Os −22%, -O2 −30% (gcc, 15 인스턴스, memcpy 있음) |
| 인스턴스 몇 개 + memcpy 를 안 쓰는 아주 작은 이미지 | fat 유지 | thin 이 +408 B (memcpy/memmove 612 B 고정비) |
| 비-trivial T, 관리 경로 | ops 표 | gcc Os −21%, 원소당 간접 호출 |
| 비-trivial T, 핫 패스 | fat 유지 (`best_vector`) | 인라인, 간접 호출 없음 |
| 전역 벡터 | 객체에 포인터 저장 금지 | 저장하면 clang `.data` 264 B / gcc `.init_array` |
| LLVM 툴체인 | 추가로 `--icf=all/safe` | fat-3N-3S −9% |

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution   # 48/48 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer solution.cpp -o sol_san && ./sol_san   # 48/48, 보고 없음
```

정답에서 채운 곳은 코어 12개 함수 본문뿐이다 (diff 에 강조). 래퍼(`vec_ref`, `static_vector`, `THIN_INLINE`), fat, 테스트 타입, 하네스는 starter 와 같다. `demo_vec.hpp`/`demo_thin_core.cpp` 는 이 파일의 fat·thin 을 그대로 옮긴 것이다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 코어를 헤더에 `inline` 으로 둠 | 호출처마다 다시 인라인 → bloat 복귀 (clang -O2 thin 3338 → 3830) | 코어는 별도 .cpp (LTO 를 켜면 다시 확인) |
| 래퍼를 "어차피 한 줄이니 인라인되겠지" | gcc -Os 가 (T,N) 별 forwarder 를 함수로 뺐다 (`static_vector<T,N>::…` 75개, 1364 B) | `[[gnu::always_inline]]` |
| 객체에 `std::byte* buf_` 저장 (ETL/LLVM 식) | 전역이 `.data` 로 (flash 이미지) 또는 `.init_array` 로 | 저장하지 말고 `(size_, storage_)` 를 인자로 / 값 뷰 |
| 기본 복사 생성자 허용 + 포인터 저장 설계 | 복사본의 `buf_` 가 **원본 저장소**를 가리킨다 | 복사 `= delete` 또는 직접 구현 |
| `is_trivially_copyable` 만으로 memcpy 허용 | 복사 생성자 delete 된 타입도 true → 금지된 복사 | `MemcpyElem` + `std::copyable` |
| 별칭 insert 를 안 고침 | `insert(0, v[2])` 가 엉뚱한 값을 넣는다 | `in_range` 면 `src += esz` |
| 포인터 구간 판정에 내장 `<` | 서로 다른 객체의 포인터 비교는 unspecified | `std::less<const std::byte*>` |
| 패딩 있는 구조체를 바이트로 CRC/전송 | 패딩 값이 정해지지 않아 같은 값 ≠ 같은 바이트 | 패딩을 명시 필드로, `has_unique_object_representations` 로 막기 |
| `--gc-sections` 만 켬 | 비템플릿 코어의 안 쓰는 함수는 같은 `.text` 섹션이라 안 지워진다 (3872 B) | `-ffunction-sections` 와 함께 (3540 B) |
| `nm` 상위에 템플릿이 없으니 bloat 없음 | -O2 fat 은 멤버를 사용자 함수에 녹인다 (`lib 0, app 7746`) | 사용자 함수 크기까지, 구현 바꿔 A/B 측정 |
| "ICF 가 알아서 합쳐 준다" | GNU ld 에는 없고, lld 도 바이트가 같은 함수만 (N 이 들어간 함수는 다름) | 설계로 해결, ICF 는 덤 |

## 8. 면접 Q&A

<details>
<summary>Q1. 템플릿 bloat 은 정확히 무엇에 비례하나요? 객체 수인가요?</summary>

**구별되는 인스턴스화 (T, N, 정책…) 수 × 실제로 쓰인 멤버 함수 수** 다 — 객체 수가 아니다. 클래스 템플릿의 멤버 함수는 쓰일 때만 인스턴스화된다 ([temp.inst]). 거기에 인라인이 곱해진다: -O2 에서 fat 은 멤버를 호출처마다 펼쳐서 3N-3S 가 24014 B 였다 (clang). 같은 (T,N) 을 여러 번역 단위에서 써도 COMDAT 으로 한 벌만 남는다. 꼬리: "그럼 `static_vector<uint32_t, 16>` 과 `<int32_t, 16>` 은?" → 다른 인스턴스지만 기계어가 같으면 lld `--icf` / ld64 dedup 이 접는다 (실측). GNU ld 는 못 접는다.

</details>

<details>
<summary>Q2. thin template 이 공짜인가요? 무엇을 내주나요?</summary>

네 가지: (1) 호출처마다 인자 준비 + `bl` — `geom` 을 레지스터 하나로 묶어 줄였다 (`movs/movt`). (2) 코어 안의 런타임 곱셈 (`i * esz`), 컴파일 타임 상수 전파를 잃는다 — 4바이트 원소의 memcpy 가 `ldr/str` 이 아니라 libc 호출이 된다. (3) memcpy/memmove 링크 고정비 (newlib 612 B). (4) 비-trivial T 의 ops 표는 원소마다 간접 호출. 그래서 실측 손익분기가 있다: memcpy 없는 이미지에서 인스턴스 5개는 fat 이 1312 B, thin 이 1720 B 였다. 꼬리: "속도는?" → 핫 루프에서 원소 하나 push 가 함수 호출이 된다. ISR 의 바이트 큐 같은 핫 패스는 전용 링 버퍼(DS 트랙)나 fat 을 쓴다.

</details>

<details>
<summary>Q3. ETL 은 ivector 에 버퍼 포인터를 저장하잖아요. 왜 안 그랬나요?</summary>

전역 배치를 재 보니 포인터 하나 때문에 객체가 "0 이 아닌 초기값" 이 되어 clang -Os/-O2 에서 64 × u32 벡터 264 B 가 통째로 `.data` (flash 이미지 + RAM 사본) 로 갔다. gcc 는 `.bss` + `.init_array` 동적 초기화로 보냈고, constexpr 생성자로 constinit 을 만들면 둘 다 `.data` 였다. 대신 포인터 없이 `{size, storage}` 만 두고, N 을 지운 접근은 값 뷰 `vec_ref<T>` 로 만들었다. 내준 것: 뷰가 원본보다 오래 살면 dangling (span 과 같은 규칙), 그리고 `ivector&` 처럼 "진짜 객체에 대한 참조" 를 넘길 수 없다. ETL 전역이 실제로 어디 놓이는지는 ETL 로 측정하지 않았다(미확인) — 같은 모양의 `PtrVec` 로만 쟀다. LLVM `SmallVector` 는 힙으로 자랄 수 있어서 포인터가 필수다 (`BeginX`).

</details>

<details>
<summary>Q4. 비-trivial 원소는 어떻게 하죠? memcpy 하면 안 되잖아요.</summary>

두 길. (a) 타입별 연산 표: `copy_construct/move_construct/copy_assign/move_assign/destroy` 함수 포인터 5개를 `inline constexpr elem_ops ops_for<T>` 로 `.rodata` 에 둔다 (실측 20 B + 함수 5개 ≈ 100 B, N 무관). 코어는 원소마다 간접 호출. (b) fat 템플릿 유지. 크기는 (a) 가 이겼다 (gcc Os 4556 → 3616) 하지만 루프에 `blx` 가 생긴다. `std::function`/vtable 도 같은 아이디어이고, 이건 손으로 만든 vtable 이다 (CPP-M06 의 vtable 비용 참고). 꼬리: "trivially relocatable 이면?" → 이동+소멸을 memcpy 로 대신할 수 있는 타입 (예: `unique_ptr` 류). 표준화 시도(P1144/P2786)가 있었지만 이 레슨에서 상태를 확인하지 않았다(미확인).

</details>

<details>
<summary>Q5. insert 의 인자가 자기 원소를 가리키면 왜 문제고, 어떻게 고쳤나요?</summary>

뒤를 한 칸 미는 순간 그 원소가 이동한다. 템플릿이면 `T tmp(v)` 로 먼저 복사하면 끝이지만 바이트 코어는 T 를 모르니 임시를 못 만든다 (VLA 금지, 힙 금지). 그래서 `src` 가 `[i, size)` 안이면 `src += esz` — LLVM `SmallVector::insert_one_impl` 의 `isReferenceToRange(...) → ++EltPtr` 와 같은 방법. 구간 판정은 `std::less` 로 (내장 `<` 는 다른 객체 포인터끼리 unspecified). ops 경로도 같다: 뒤에서부터 `move_assign` 하면 원본 값은 한 칸 뒤에 있다. 꼬리: "src 가 i 바로 앞이면?" → 안 밀리니 보정하지 않는다 (테스트 `insert_own(v, 4, 1)`).

</details>

<details>
<summary>Q6. 리팩터링 전후 동작이 같다는 걸 어떻게 보장했나요?</summary>

같은 xorshift32 시드로 만든 연산열을 fat 과 thin 에 동시에 주고 매 단계 반환값과 전체 내용을 비교했다. 연산 분포가 경계를 일부러 밟게 했다: 인덱스는 `[0, size+1]` (범위 밖 포함), push 비중을 높여 꽉 찬 상태에서 6397번 실패, 별칭 insert 3522번, `N = 0`. 비-trivial T 는 주소 장부로 이중 생성/죽은 칸 대입까지 본다. 돌연변이 4개 중 범위 검사 off-by-one 하나는 **이 테스트만** 잡았다. 꼬리: "랜덤이면 재현은?" → 시드 고정, 실패 시 step 번호와 연산을 찍는다 (`mismatch at step 7 (op 2, i 1)`).

</details>

<details>
<summary>Q7. -ffunction-sections 와 --gc-sections 는 각각 무엇을 하고, thin 에서 왜 더 중요한가요?</summary>

`-ffunction-sections` 는 함수마다 `.text.<이름>` 섹션을 만들고, `--gc-sections` 는 진입점에서 닿지 않는 섹션을 링크에서 버린다. 링커는 섹션 단위로만 버린다. 템플릿 인스턴스는 원래 COMDAT 그룹이라 따로 섹션이 있지만, 비템플릿 코어 .cpp 의 함수들은 플래그 없이는 한 `.text` 에 모인다 → 안 쓰는 `*_ops` 5개가 남았다 (gc 만: 3872 B, ops 5개 / 둘 다: 3540 B, 0개). 꼬리: "부작용은?" → 섹션이 잘게 나뉘어 함수 간 상대 분기/리터럴 공유가 줄 수 있다. gcc 에서는 `-fdata-sections` 유무로도 fat `.text` 가 4876 → 4524 로 달라졌다 (함수 본문 크기가 달라짐 — 전역 주소를 섹션 앵커로 계산하는 방식 차이로 추정, 미확인).

</details>

<details>
<summary>Q8. LTO 나 ICF 를 켜면 thin template 이 필요 없지 않나요?</summary>

ICF 는 바이트가 같은 함수만 접는다: N 이 비교 상수로 들어간 `push_back/insert` 는 N 마다 달라서 못 접고, 인라인되어 사용자 함수에 녹은 코드도 대상이 아니다. 실측으로 lld `--icf=all` 이 fat-3N-1S 에선 0 B, 호출처가 많아 멤버가 함수로 남은 3N-3S 에선 −832 B. 그리고 arm-none-eabi GNU ld 엔 ICF 자체가 없다. LTO 는 오히려 반대로 갈 수 있다: 코어까지 호출처에 인라인하면 bloat 이 돌아온다 (헤더 inline 실험이 그 모양) — LTO 빌드는 이 레슨에서 측정하지 않았다(미확인). 설계로 줄이고, 도구는 덤.

</details>

## 9. 추가 연습

### 연습 1 — `best_vector<T,N>`: trivial 이면 thin, 아니면 fat

5. 풀이의 결정표 "비-trivial 은 핫 패스면 fat" 을 타입 별칭으로 만들어라. `Counted`(복사 시 카운트)는 fat, `uint32_t`/`CanFrame` 은 thin 이어야 한다.

<details>
<summary>답 보기</summary>

```cpp
template <class T, std::size_t N>
using best_vector = std::conditional_t<thin::MemcpyElem<T>, thin::static_vector<T, N>, fat::static_vector<T, N>>;
static_assert(std::is_same_v<best_vector<Counted, 8>, fat::static_vector<Counted, 8>>);
```

두 구현의 공개 API 가 같아서(계약을 맞춰 둔 덕) 사용 코드는 바뀌지 않는다. 단, `vec_ref<T>` 로의 변환은 thin 쪽에만 있다 — 비-trivial 경로를 받는 함수는 템플릿이 되어야 한다는 것도 트레이드오프다.

</details>

### 연습 2 — `erase_if`: 한 번 훑는 바이트 코어

`drop_ids` 는 지울 때마다 뒤를 당겨서 O(n²) 이다. `erase_if_raw(size&, buf, esz, bool(*pred)(const void*, void*), void* ctx)` 를 한 번에 압축하는 O(n) 코어로 쓰고, 템플릿 술어를 받는 얇은 `erase_if(vec_ref<T>, Pred)` 를 얹어라. 64개 중 홀수 id 를 지울 때 옮긴 바이트를 비교하라.

<details>
<summary>답 보기</summary>

```cpp
std::size_t w = 0;
for (std::size_t r = 0; r < size; ++r) {
    std::byte* e = buf + r * esz;
    if (pred(e, ctx)) continue;
    if (w != r) counted_move(buf + w * esz, e, esz);
    ++w;
}
```

`(T, Pred)` 마다 생기는 것은 캡처 없는 람다 트램펄린 하나 (`[](const void* e, void* c){ return (*static_cast<Pred*>(c))(*std::launder(static_cast<const T*>(e))); }`). 실제 출력: `removed 36 / 64, moved bytes: erase_if 448, erase loop 20080` — 45배. 크기 bloat 과 별개로 thin 코어가 알고리즘을 한 군데에서 고치게 해 준다. `std::erase_if`(C++20) 의 remove-erase 와 같은 모양이다. 연습 파일은 이를 위해 `vec_ref::parts()` 접근자 하나를 추가했다.

</details>

### 연습 3 — 원소 타입 분류: 어느 경로로 저장하나

`elem_path<T>()` 가 `MemcpyBytes`(memcpy + `bytes()`), `MemcpyOnly`, `Ops`, `Rejected` 중 하나를 돌려주게 하라. 표의 타입을 **먼저 예상하고** 컴파일러에게 물어 확인하라.

<details>
<summary>답 보기</summary>

| 타입 | 예상하기 쉬운 답 | Apple clang 21 실측 | gcc 15 (호스트) |
|---|---|---|---|
| `{u8; u32}` (패딩) | MemcpyOnly | MemcpyOnly | 같음 |
| `{float}` | MemcpyBytes? | **MemcpyOnly** — float 은 unique representation 아님 | 같음 |
| `{u32 a:3}` | ? | MemcpyOnly (안 쓰는 29비트) | 같음 |
| `{volatile u32}` | Ops? | **MemcpyBytes** — trivially copyable | 같음 |
| `{const u32}` | MemcpyBytes? | **Rejected** — 대입 불가라 `std::copyable` 아님 | 같음 |
| `{u32& r}` | — | unique = **true** | unique = **false** (컴파일러마다 다름) |

참조 멤버 타입은 두 컴파일러가 `has_unique_object_representations` 를 다르게 답해서 테스트에 넣지 않았다. "규칙을 외우지 말고 `static_assert` 로 묻는다" 가 요점이다.

</details>

### 연습 4 — CAN 송신 대기열: 우선순위 정렬 삽입

id 가 작을수록 먼저 나가는 TX 대기열에 `insert_sorted(vec_ref<CanFrame>, f)` 를 써라. 같은 id 는 FIFO, 꽉 차면 `false`. 8칸·3칸 큐에 같은 함수를 쓴다.

<details>
<summary>답 보기</summary>

```cpp
std::size_t lo = 0, hi = q.size();
while (lo < hi) {                       // upper_bound: 같은 id 뒤에 넣어야 FIFO
    const std::size_t mid = lo + (hi - lo) / 2;
    if (q[mid].id <= f.id) lo = mid + 1; else hi = mid;
}
return q.insert(lo, f);
```

`<=` 가 핵심이다 (`<` 면 lower_bound 라 같은 id 의 앞에 들어가 순서가 뒤집힌다). 3칸 큐는 `0x300, 0x100, 0x200` 이 들어간 뒤 꽉 차서 나머지를 거절한다 — 거절된 프레임을 어떻게 할지(오래된 것 교체? 카운터?)는 호출자 정책이다. 이 함수는 N 이 몇이든 한 벌이다.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 13/13 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer practice.cpp -o practice_san && ./practice_san   # 13/13, 보고 없음
```

## 10. 핵심 정리

- 템플릿 bloat ∝ (구별되는 인스턴스화 수) × (쓰인 멤버) × (인라인 복제). -O2 에서는 컨테이너 심볼이 아니라 사용자 함수 크기로 숨는다.
- thin template = T 는 `esz`, N 은 `cap` 으로 **값**이 된 비템플릿 코어(별도 .cpp, 한 벌) + `[[gnu::always_inline]]` 래퍼. 실측: clang -O2 −58%, gcc -Os −22% (15 인스턴스), 래퍼·컨테이너 심볼 0개.
- 고정비가 있다: 코어 ≈ 200 B + memcpy/memmove 612 B. 인스턴스가 적고 memcpy 가 없는 이미지에선 fat 이 작다. 측정하고 고른다.
- 객체에 버퍼 포인터를 저장하지 마라: 전역이 `.data`(clang) 나 `.init_array`(gcc) 로 간다. `{size, storage}` + 값 뷰 `vec_ref<T>`.
- memcpy 코어는 `MemcpyElem` (trivially copyable + trivially copy constructible + trivially destructible) 에만. 바이트로 내보내기는 `has_unique_object_representations` 까지. 비-trivial 은 ops 표(작지만 간접 호출) 또는 fat.
- 별칭 insert: 밀린 자기 원소는 `src + esz` 에. 구간 판정은 `std::less`.
- 리팩터링 증명 = 같은 연산열 동치 테스트. 단위 테스트가 놓친 off-by-one 을 잡았다.
- ICF: GNU ld 없음, lld `--icf` 는 바이트가 같은 함수만 (N 들어간 함수는 못 접음), ld64 는 기본 dedup (호스트 전용). 비템플릿 코어는 `-ffunction-sections` 가 있어야 gc 된다.

## 참고자료

- C++ 표준(N4861 기준): [temp.inst] (멤버 함수의 지연 인스턴스화), [cstring.syn]·[intro.object] (memcpy 의 암시적 객체 생성, P0593R6), [ptr.launder], [expr.rel] (다른 객체 포인터 비교 unspecified), [comparisons.general] (`std::less` 전순서), [meta.unary.prop] (`is_trivially_copyable`, `has_unique_object_representations`), [expr.prim.req] (requires-식), [concepts.object] (`std::copyable`)
- ETL `include/etl/vector.h` — `ivector<T>` 의 `p_buffer`/`p_end`, `vector() : ivector<T>(reinterpret_cast<T*>(&buffer), MAX_SIZE)`: https://github.com/ETLCPP/etl
- LLVM `llvm/include/llvm/ADT/SmallVector.h` — `SmallVectorBase` (`BeginX, Size, Capacity`, `grow_pod`), `SmallVectorTemplateBase<T, bool>` trivially-copyable 특수화, `insert_one_impl` 의 `isReferenceToRange → ++EltPtr`: https://github.com/llvm/llvm-project
- LLD ELF 옵션 `--icf=all|safe|none`, `--print-icf-sections`: https://lld.llvm.org/
- GCC 문서: `-ffunction-sections`, `-fipa-icf`, `always_inline` 속성 — https://gcc.gnu.org/onlinedocs/gcc/
- ARM AAPCS (IHI 0042) — 4바이트 이하 복합 타입의 레지스터 전달
- C++26 `std::inplace_vector` (P0843) — 표준 고정 용량 벡터, 구현의 thin 여부는 라이브러리 몫
- 관련 레슨: CPP-M10 (static_vector 본체), CPP-M07 (저장소·launder), CPP-M05 (CRTP bloat, `frame_core`), CPP-M06 (vtable 비용), CPP-B09 (`.init_array` 순서), CPP-A03 (`__dso_handle`·`__aeabi_atexit`), MCPP-A09 (빌드 시간), DS 트랙 (링 버퍼)
