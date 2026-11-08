---
id: C-A06
time: 90분
updated: 2026-11-08
---
# C-A06 offsetof와 container_of 구현

## 학습 목표

- `<stddef.h>` 없이 `offsetof` 를 **세 가지 방식**(고전 널 포인터 형태 / 실제 저장소 기준 / 컴파일러 내장)으로 만들고, 각각이 UB 인지, 정수 상수식(ICE)인지, UBSan 이 무엇을 잡는지 실측으로 말할 수 있다 (C11 6.6p6, 7.19p3).
- 타입 검사가 들어간 `container_of` 를 순수 C11 로 구현하고, 그 검사가 **잡는 것**(다른 타입, 배열 전체를 멤버로 줌)과 **못 잡는 것**(같은 타입의 다른 멤버 이름)을 구분해, 못 잡는 경우를 타입 설계로 막는다.
- `_Generic` 과 조건 연산자의 한정자 규칙(6.5.15p6)으로 **const 를 보존하는** `container_of_const`, 그리고 **NULL 안전 + 인자 1회 평가** `container_of_or_null` 을 만든다.
- 배열 원소 멤버(`ch[i]`)에서 바깥 구조체를 찾는 두 방법(`c - c->idx` 로 `ch[0]` 기준 / 런타임 인덱스 멤버 지정자)의 차이와 표준 근거를 설명한다.
- DMA 완료 ISR → 워크큐 bottom half 구조에서 "하드웨어가 준 주소 하나" 로 드라이버 인스턴스를 복원하고, 실제 칩에서 추가로 필요한 것(주소 검증, 버스 주소 ≠ CPU 주소, 캐시, 임계 구역)을 짚는다.

## 1. 문제

Linux, Zephyr, 대부분의 RTOS 드라이버는 콜백에 `void *ctx` 대신 **내장된 멤버의 주소**(`struct k_work *`, `struct device *`, `struct list_head *`)만 넘기고, 받는 쪽이 `container_of` 로 주인 구조체를 되찾는다. 그래서 시니어 펌웨어 면접에서 "offsetof 를 직접 구현해 보세요 → 그거 UB 아닌가요? → container_of 는요? → const 포인터를 넣으면?" 으로 이어지는 꼬리질문이 단골이다. DS-M01(침입형 리스트)에서 `container_of` 를 **썼다면**, 이 레슨은 그 매크로 자체를 **설계** 한다.

시나리오: UART 드라이버가 송신 요청 4개(`tx[4]`)를 갖고, 각 요청 안에 DMA 하드웨어가 읽는 디스크립터(`desc`)가 들어 있다. DMA 가 끝나면 엔진은 **방금 끝낸 디스크립터의 주소** 를 레지스터 `CURR_DESC` 에 남기고 인터럽트를 건다. ISR 은 그 주소 하나만으로 "어느 UART 의 몇 번 요청인가" 를 알아내야 하고, 무거운 일은 워크큐(`struct k_work`)로 미룬다. 워크큐는 `handler(&work)` 만 부른다 — 주인이 UART 인지 모른다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">struct uart_dev (Cortex-M4 레이아웃, 160 B) — 주소 하나에서 주인을 거꾸로 찾는다</text>
  <g stroke="currentColor">
    <rect x="20" y="50" width="227" height="40" class="f-mut"/>
    <rect x="247" y="50" width="105" height="40" fill="none"/>
    <rect x="352" y="50" width="105" height="40" fill="none"/>
    <rect x="457" y="50" width="105" height="40" class="f-acc"/>
    <rect x="492" y="56" width="68" height="28" class="f-warn"/>
    <rect x="562" y="50" width="105" height="40" fill="none"/>
    <rect x="667" y="50" width="53" height="40" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="30" y="74">name·base·카운터·done_cookie</text>
    <text x="270" y="74" font-family="monospace">tx[0]</text>
    <text x="375" y="74" font-family="monospace">tx[1]</text>
    <text x="462" y="74" font-family="monospace">tx[2]</text>
    <text x="500" y="74" font-family="monospace">desc</text>
    <text x="585" y="74" font-family="monospace">tx[3]</text>
    <text x="672" y="74" font-family="monospace">work</text>
    <text x="16" y="106" font-family="monospace">0</text>
    <text x="240" y="106" font-family="monospace">52</text>
    <text x="446" y="106" font-family="monospace">100</text>
    <text x="486" y="106" font-family="monospace">108</text>
    <text x="656" y="106" font-family="monospace">148</text>
    <text x="704" y="106" font-family="monospace">160</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M492 128 L458 128 M458 128 l8 -4 M458 128 l8 4"/>
    <path d="M458 152 L248 152 M248 152 l8 -4 M248 152 l8 4"/>
    <path d="M248 176 L21 176 M21 176 l8 -4 M21 176 l8 4"/>
    <path d="M668 200 L21 200 M21 200 l8 -4 M21 200 l8 4"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="452" y="124" text-anchor="end" font-family="monospace">CURR_DESC = dev+108 → −8 → &amp;tx[2]</text>
    <text x="300" y="146" font-family="monospace">r − r-&gt;idx (2×24 B) → &amp;tx[0]</text>
    <text x="60" y="170" font-family="monospace">−52 (tx[0]) → dev</text>
    <text x="300" y="194" font-family="monospace">k_work * = dev+148 → −148 → dev</text>
  </g>
  <rect x="10" y="222" width="720" height="68" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="244" fill="currentColor">ISR: desc → tx_req → uart_dev 를 오프셋 뺄셈만으로. bottom half: k_work → uart_dev.</text>
  <text x="22" y="264" fill="currentColor">오프셋은 컴파일 타임 상수 → 명령어 1~5개 (5. 풀이의 실측 asm). 포인터 저장 공간 0 B.</text>
  <text x="22" y="282" fill="currentColor">arm64(이 Mac)는 포인터가 8 B 라 tx 는 56, work 는 184 — 숫자는 달라도 코드는 같다.</text>
</svg>
```

모든 코드는 하나의 파일(`starter.c`)에 있다. 테스트 심판은 `<stddef.h>` 의 `offsetof` 와 실제 객체 주소(`&obj`)로 판정하므로, 여러분의 매크로가 틀려도 판정은 정확하다.

### Part A — MY_OFFSETOF 와 container_of

- `MY_OFFSETOF(type, member)` — `offsetof` 를 쓰지 않는다. 고전 형태 `((size_t)&((type *)0)->member)` 도 **금지** (5. 풀이에서 UBSan 이 실제로 잡는 걸 본다). 멤버 지정자는 `flags`, `in.y`, `arr[3]`, `arr[i]`(런타임 i), 유연 배열 멤버 `data`, 공용체 멤버 모두 돼야 한다. 결과 타입은 `size_t`. **정수 상수식일 필요는 없다** — 왜 일반적으로 불가능한지가 풀이의 핵심 중 하나다.
- `container_of(ptr, type, member)` — `MY_OFFSETOF` 로 만들고, `ptr` 의 타입이 `type` 의 `member` 와 다르면 **컴파일 진단**. 실행 비용 0, `ptr` 은 한 번만 평가.
- 테스트: 첫 멤버(offset 0), 중간, 중첩(`nest.deep`), 마지막 멤버, 배열 안의 객체 4개 각각에서 원래 주소로 돌아오는지. 그리고 "같은 타입의 다른 멤버 이름" 을 주면 **정확히 몇 바이트 어긋나는지** 를 고정한다 (타입 검사가 못 잡는 경우).

### Part B — const, NULL, 배열 원소

- `container_of_const(ptr, type, member)` — `const` 멤버 포인터를 넣으면 `const type *`, 아니면 `type *`. 평범한 `container_of` 는 const 를 **조용히** 떨어뜨린다 (테스트가 `_Generic` 으로 확인).
- `container_of_or_null(ptr, type, member)` — `NULL` 이면 `NULL`. `ptr` 이 부작용 있는 식이어도 **정확히 1회** 평가 (`next_node()` 호출 횟수로 검사). 함수 `cof_sub_or_null_()` 를 채운다.
- `chan_to_ctrl(c)` / `chan_to_ctrl_idx(c)` — `struct dma_ctrl { ...; struct dma_chan ch[4]; }` 의 채널 포인터에서 컨트롤러로. 채널은 자기 인덱스 `c->idx` 를 안다. 두 가지 방법으로 구현한다.

### Part C — DMA 완료 ISR + 워크큐

주어진 코드: 워크큐(`work_submit`/`work_run_all`, 대기 중이면 합침), `dma_irq_handler()`(ISR), bottom half `uart_tx_work_handler()`, 하드웨어 시뮬레이터. 채울 것 세 개:

| 함수 | 입력 → 출력 | 주의 |
|---|---|---|
| `req_from_desc(d)` | `struct dma_desc *` → `struct tx_req *` | `CURR_DESC == 0` 인 가짜 인터럽트 → `NULL` |
| `uart_from_req(r)` | `struct tx_req *` → `struct uart_dev *` | `r` 은 `dev->tx[r->idx]` |
| `uart_from_work(w)` | `struct k_work *` → `struct uart_dev *` | 워크큐는 주인 타입을 모른다 |

시나리오 4개: uart1 단일 완료, uart0 연속 2회 완료(work 합쳐져 bottom half 1번에 2개 회수), 두 장치 교차, `CURR_DESC = 0` 가짜 인터럽트(크래시 없이 `g_spurious++`).

## 2. Problem (English)

> **Implement `offsetof` and `container_of` without `<stddef.h>`'s `offsetof`.**
>
> 1. `MY_OFFSETOF(type, member)` → `size_t` byte offset of `member` (any member-designator: `a`, `a.b`, `a[3]`, `a[i]` with runtime `i`, flexible array member, union member). Must be free of undefined behavior and clean under `-fsanitize=undefined`; the classic `((size_t)&((type *)0)->member)` is not allowed. It need not be an integer constant expression.
> 2. `container_of(ptr, type, member)` → `type *` of the object containing `*ptr`. Zero runtime cost, `ptr` evaluated once, and a compile-time diagnostic if `ptr` does not have type "pointer to the type of `member`".
> 3. `container_of_const` (returns `const type *` iff `ptr` points to const), `container_of_or_null` (NULL in → NULL out, `ptr` evaluated exactly once), and `chan_to_ctrl(c)` for a `struct dma_chan` stored in `struct dma_ctrl::ch[4]`, where `c->idx` is its index.
> 4. A DMA engine reports the address of the completed descriptor in `CURR_DESC`. Descriptors are embedded in `struct tx_req`, which are embedded in `struct uart_dev::tx[4]`; a `struct k_work` is embedded in `uart_dev`. Implement `req_from_desc`, `uart_from_req`, `uart_from_work` so the ISR and the bottom half find the right device instance.
>
> Example (arm64): `MY_OFFSETOF(struct layout, in.y) == 24`; `container_of(&h.nest.deep, struct node_host, nest.deep) == &h`; `container_of(&h.mid, struct node_host, last)` compiles silently and returns `(char *)&h - 48`.
>
> Follow-ups: Is the classic null-pointer `offsetof` UB? Why is `offsetof` a library macro at all? What does your type check miss? Is `container_of` itself well-defined? What changes if `CURR_DESC` holds a *bus* address, or if the descriptor lives in cacheable memory on a Cortex-M7?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "7/42 passed" — offset 0 인 경우와 "평범한 container_of 는 const 를 잃는다" 같은 사실 확인만 통과
```

스텁은 크래시 없이 틀린 값만 낸다: `MY_OFFSETOF` 는 항상 0, `container_of` 는 그냥 캐스트(= offset 0 일 때만 맞다), Part B/C 함수는 `NULL`. ISR 과 bottom half 는 `NULL` 을 받으면 `g_spurious++` 하고 돌아가도록 이미 작성돼 있다.

관찰용 파일 세 개 (하네스와 별개, 결과는 5. 풀이에):

```bash
cc -std=c11 -Wall -Wextra -O2 demo_offsetof_variants.c -o /tmp/C-A06_dv && /tmp/C-A06_dv            # 세 방식 값 비교
cc -std=c11 -Wall -Wextra -pedantic -O2 -DICE_USE demo_offsetof_variants.c -o /tmp/C-A06_dv        # 상수식 자리에 쓰기
cc -std=c11 -g -O1 -fsanitize=undefined demo_offsetof_variants.c -o /tmp/C-A06_dvu && /tmp/C-A06_dvu  # 널 형태를 UBSan 으로
cc -std=c11 -Wall -Wextra -c -DBAD_INT demo_bad_types.c -o /dev/null      # BAD_* 8가지: 무엇이 경고/에러가 되나
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -O2 -S demo_m4.c -o -              # Cortex-M4 asm
```

## 4. 힌트

<details>
<summary>힌트 1 — 기준점이 꼭 0번지여야 하나?</summary>

오프셋 = `(멤버 주소) − (구조체 시작 주소)`. 고전 형태는 시작 주소를 0 으로 놓아 뺄셈을 생략한 것이다. 시작 주소가 **실제로 존재하고, 정렬·크기가 충분한 저장소** 라면 뺄셈을 하면 된다. 같은 배열 객체 안을 가리키는 두 `unsigned char *` 의 뺄셈은 정의된 동작이다 (6.5.6p9). 저장소가 작거나 정렬이 부족한 타입은 **컴파일 에러** 로 막을 방법도 생각해 보자 — `sizeof(char[조건 ? 1 : -1])`.

</details>

<details>
<summary>힌트 2 — container_of 의 타입 검사는 "실행되지 않는 비교"</summary>

`sizeof` 의 피연산자는 (VLA 가 아니면) 평가되지 않는다 (6.5.3.4p2). 그러니 `(void)sizeof((ptr) == &((type *)0)->member)` 는 실행 코드 0, 널 역참조 0 이면서, 두 포인터 타입이 다르면 6.5.9p2 제약 위반 → 진단. 주소 계산은 반드시 `char *` 로 바꿔서 바이트 단위로 뺀다.

</details>

<details>
<summary>힌트 3 — const 는 조건 연산자에게 물어본다</summary>

`1 ? (p) : (void *)(p)` 의 타입은? 한쪽이 `void *`(널 포인터 상수가 아님)이면 결과는 "양쪽 한정자를 합친 `void *`" 다 (6.5.15p6). `p` 가 `const T *` 면 `const void *`. 이걸 `_Generic` 의 제어식으로 쓰면 `const void *:` 가지와 `default:` 가지를 고를 수 있다. 제어식은 평가되지 않는다.

</details>

<details>
<summary>힌트 4 — ch[i] 에서 바깥으로</summary>

`c` 가 `ch[c->idx]` 라면 `c - c->idx` 는 `&ch[0]` 이다 (같은 배열 안 포인터 산술). 그다음은 멤버 `ch[0]` 에 대한 평범한 `container_of`. `ch` (배열 전체)를 멤버로 주면 `&((T *)0)->ch` 의 타입이 `struct dma_chan (*)[4]` 라 타입 검사에 걸린다. 다른 방법: 멤버 지정자에 런타임 인덱스를 그대로 — `ch[c->idx]`. Part C 의 `uart_from_req` 도 같은 모양이다.

</details>

## 5. 풀이

### Part A-1 — offsetof 를 만드는 세 가지 방법

| | ① 고전 널 형태 | ② 저장소(arena) 기준 (이 레슨) | ③ `<stddef.h>` offsetof |
|---|---|---|---|
| 식 | `((size_t)&((T *)0)->m)` | `(size_t)((unsigned char *)&((T *)arena)->m - arena)` | `__builtin_offsetof(T, m)` (이 Mac) |
| 표준상 | **UB** (널 포인터로 멤버 접근) + 포인터→정수 변환은 구현 정의 | 접근 없음, 같은 배열 안 뺄셈 | 정의됨 (구현이 제공) |
| 정수 상수식(ICE) | 아니다 (6.6p6) — clang 이 확장으로 접어 줌 | 아니다 | **맞다** (7.19p3) |
| UBSan | `member access within null pointer` | 깨끗 | 깨끗 |
| 런타임 인덱스 `a[i]` | 동작 (UB 그대로) | 동작 | clang 은 받아 줌 (표준 밖, 아래) |
| 쓰는 곳 | 옛 newlib/glibc, 옛 임베디드 컴파일러 헤더 | 사용자 코드에서 offsetof 금지일 때 | 실무 기본값 |

`demo_offsetof_variants.c` 실제 출력 — 값은 셋 다 같다:

```text
            seq  hdr.len  payload[i=3]
builtin  :    4        8       15
null form:    4        8       15
arena    :    4        8       15
```

차이는 **값이 아니라 지위** 다. 같은 파일을 UBSan 으로 돌리면 (실제 출력):

```text
demo_offsetof_variants.c:37:43: runtime error: member access within null pointer of type 'struct pkt'
demo_offsetof_variants.c:37:70: runtime error: member access within null pointer of type 'struct pkt'
demo_offsetof_variants.c:38:12: runtime error: member access within null pointer of type 'struct pkt'
```

37–38 행은 `null form` 줄이다. 상수 멤버(`seq`)조차 런타임에 계측됐다. arena 줄과 builtin 줄은 보고가 없다.

**왜 ① 은 UB 인가.** `E->m` 은 "E 가 가리키는 **객체** 의 멤버" 를 지정한다 (6.5.2.3p4). 널 포인터는 어떤 객체도 가리키지 않는다 (6.3.2.3p3). 단항 `*` 에 널을 주면 UB 이고 (6.5.3.2p4), `&*E` 는 예외로 정의돼 있지만(6.5.3.2p3) `&E->m` 에 대한 예외는 없다. 그다음 포인터→정수 변환 결과는 구현 정의다 (6.3.2.3p6). 그럼에도 수십 년 쓰인 이유: **구현(컴파일러+라이브러리)은 자기 컴파일러가 이걸 어떻게 처리하는지 알기 때문에** 써도 된다. 이 Mac 의 clang 헤더에도 그 흔적이 남아 있다 (실제 grep):

```text
/Library/Developer/CommandLineTools/usr/lib/clang/21/include/__stddef_offsetof.h:16:#define offsetof(t, d) __builtin_offsetof(t, d)
/Library/Developer/CommandLineTools/usr/lib/clang/21/include/profile/InstrProfData.inc:548:#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
```

**왜 사용자 매크로로는 ICE 를 못 만드나.** 6.6p6: 정수 상수식의 피연산자는 정수/열거/문자 상수, ICE 결과의 `sizeof`, `_Alignof` 등뿐이고, 캐스트는 "산술 타입 → 정수 타입" 만 허용된다. 포인터가 끼는 순간 ICE 가 아니다. 그래서 `offsetof` 는 **라이브러리 매크로(=구현의 일)** 로 표준에 들어 있다. 6.6p10 은 "구현은 다른 형태의 상수식을 받아들여도 된다" 고 하므로 clang 이 접어 주는 건 합법적 확장이고, `-pedantic` 이 그걸 알려 준다 (`-DICE_USE` 실제 출력):

```text
$ cc -std=c11 -Wall -Wextra -O2 -DICE_USE demo_offsetof_variants.c        # 아무 말 없음
$ cc -std=c11 -Wall -Wextra -pedantic -O2 -DICE_USE demo_offsetof_variants.c
demo_offsetof_variants.c:26:16: warning: expression is not an integer constant expression; folding it to a constant is a GNU extension [-Wgnu-folding-constant]
demo_offsetof_variants.c:27:16: warning: expression is not an integer constant expression; folding it to a constant is a GNU extension [-Wgnu-folding-constant]
demo_offsetof_variants.c:28:18: warning: expression is not an integer constant expression; folding it to a constant is a GNU extension [-Wgnu-folding-constant]
```

26 = 널 형태 `_Static_assert`, 27 = arena 형태 `_Static_assert`, 28 = 널 형태 `enum`. 25 행의 표준 `offsetof` 는 경고가 없다. 정리: **컴파일 타임 계약(`_Static_assert`, `case`, 배열 크기)에는 표준 offsetof**, 이 레슨의 `MY_OFFSETOF` 는 런타임 식이다 (C-A08 에서 레이아웃 계약을 본격적으로 다룬다).

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">오프셋 = 멤버 주소 − 기준 주소. 기준을 어디에 두느냐가 세 방식의 차이</text>
  <text x="10" y="60" fill="currentColor">① 널 형태</text>
  <rect x="120" y="42" width="80" height="28" class="f-warn" stroke="currentColor"/>
  <text x="132" y="61" fill="currentColor" font-family="monospace">0x0</text>
  <rect x="200" y="42" width="260" height="28" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="212" y="61" fill="currentColor">객체 없음 — "T 라고 치고" 멤버 접근</text>
  <text x="475" y="61" fill="currentColor">UB · UBSan 보고 · ICE 아님</text>
  <text x="10" y="120" fill="currentColor">② arena</text>
  <rect x="120" y="100" width="340" height="28" class="f-ok" stroke="currentColor"/>
  <text x="132" y="119" fill="currentColor" font-family="monospace">unsigned char arena[1024] (64 정렬)</text>
  <path d="M120 140 L260 140" stroke="currentColor" fill="none"/>
  <path d="M260 140 l-8 -4 M260 140 l-8 4" stroke="currentColor" fill="none"/>
  <text x="140" y="158" fill="currentColor" font-family="monospace">&amp;((T*)arena)-&gt;m − arena</text>
  <text x="475" y="119" fill="currentColor">접근 0 · 같은 배열 안 뺄셈</text>
  <text x="475" y="139" fill="currentColor">UBSan 깨끗 · ICE 아님</text>
  <text x="10" y="196" fill="currentColor">③ 내장</text>
  <rect x="120" y="178" width="340" height="28" class="f-acc" stroke="currentColor"/>
  <text x="132" y="197" fill="currentColor" font-family="monospace">__builtin_offsetof(T, m) → 컴파일러 계산</text>
  <text x="475" y="197" fill="currentColor">정의됨 · ICE · 비트필드면 에러</text>
  <text x="10" y="238" fill="currentColor">②의 대가: sizeof(T) ≤ 1024, _Alignof(T) ≤ 64 여야 한다 → 아니면 컴파일 에러 (아래 BAD_TOO_BIG).</text>
</svg>
```

**② 의 설계.** 핵심 줄은 둘이다.

```c
static _Alignas(MY_OFFSETOF_ARENA_ALIGN) unsigned char my_offsetof_arena_[MY_OFFSETOF_ARENA_SIZE];
#define MY_OFFSETOF(type, member)                                                              \
    (sizeof(char[(sizeof(type) <= MY_OFFSETOF_ARENA_SIZE &&                                    \
                  _Alignof(type) <= MY_OFFSETOF_ARENA_ALIGN) ? 1 : -1]) - 1u +                 \
     (size_t)((unsigned char *)&((type *)(void *)my_offsetof_arena_)->member - my_offsetof_arena_))
```

| 조각 | 역할 | 근거 |
|---|---|---|
| `_Alignas(64)` 저장소 | `(type *)` 로 바꿔도 정렬이 맞다 (안 맞으면 변환 자체가 UB) | 6.3.2.3p7 |
| `sizeof(char[c ? 1 : -1]) - 1u` | 값은 0. 조건이 거짓이면 **음수 크기 배열 → 무조건 컴파일 에러** (`-Werror` 불필요) | 6.7.6.2p1 (상수 크기는 0 보다 커야) |
| `&(...)->member` | 주소만 계산, 값은 읽지 않는다 → 유효 타입(6.5p7) 문제 없음 | 6.5.3.2p3 |
| `unsigned char *` 끼리 뺄셈 | 둘 다 같은 배열 `arena` 안을 가리킨다 | 6.5.6p9 |

솔직한 단서: "char 배열 저장소를 `(type *)` 로 보고 `->member` 로 주소를 계산" 하는 것도 표준 문구로 100% 깨끗하다고 하긴 어렵다 (그 자리에 `type` 객체가 실제로 있진 않다). 그러나 널 형태와 달리 **실제로 존재하고 정렬·크기가 맞는 저장소** 이고 접근이 없으며, 컴파일러·sanitizer 모두 문제 삼지 않는다. `malloc` 직후 아직 아무것도 저장하지 않은 메모리에 `&p->m` 을 계산하는 흔한 코드와 같은 지위다. 면접에서는 "널 형태는 구현만 쓸 수 있는 트릭, 사용자 코드에서는 표준 offsetof, 그게 금지면 실제 저장소 기준" 이라고 말하면 된다.

`BAD_TOO_BIG` (2016 B 구조체) 실제 출력 — `-Werror` 없이도 멈춘다:

```text
demo_bad_types.c:70:10: error: array size is negative
```

**비트필드:** 두 방식 모두 컴파일 에러로 거절한다 (실제 출력). 7.19p3 은 offsetof 에 비트필드를 주면 UB 라고만 하는데, clang 은 친절하게 에러로 만든다.

```text
-DBAD_BITFIELD     : demo_bad_types.c:64:15: error: address of bit-field requested
-DBAD_BITFIELD_STD : demo_bad_types.c:67:35: error: cannot compute offset of bit-field 'mode'
```

**펌웨어 비용:** `demo_m4.c` 를 Cortex-M4 로 컴파일하면 상수 멤버는 `-O0` 에서도 clang 이 오프셋을 접어서 arena 를 참조하지 않는다. 하지만 런타임 인덱스(`tx[r->idx]`) 함수는 `-O0` 에서 arena 주소를 실제로 계산한다 (실제 `llvm-objdump -dr`):

```text
      68:      	movw	r0, #0x0
			00000068:  R_ARM_THM_MOVW_ABS_NC	my_offsetof_arena_
      6c:      	movt	r0, #0x0
			0000006c:  R_ARM_THM_MOVT_ABS	my_offsetof_arena_
```

그 결과 `-O0` 오브젝트의 `.bss` 는 1024 B, `-O2` 는 `.bss` 섹션이 아예 없다 (`xcrun llvm-size -A` 실측: `.bss 1024` vs 없음, `.text` 126 → 42). 디버그 빌드에서 RAM 1 KB 는 작은 MCU 에서 아픈 값이다 → arena 크기를 프로젝트의 최대 구조체에 맞추거나, 디버그 빌드에서도 런타임 인덱스 형태를 피한다. (이 Mac 에는 arm-none-eabi 링커가 없어 `--gc-sections` 후 결과는 확인하지 못했다. 참조가 없는 경우라면 `-fdata-sections` + `--gc-sections` 로 버려질 것으로 예상한다.)

### Part A-2 — container_of: 뺄셈 한 번과 실행되지 않는 비교

```c
#define container_of(ptr, type, member)                                                        \
    ((void)sizeof((ptr) == &((type *)0)->member),                                              \
     (type *)(void *)((char *)(ptr) - MY_OFFSETOF(type, member)))
```

- **주소:** `char *` 로 바꿔 바이트 단위로 `offset` 만큼 뒤로. `(type *)(void *)` 는 clang `-Wcast-align`(char* → 더 큰 정렬 타입)을 피하는 관용구다.
- **타입 검사:** `sizeof` 피연산자는 평가되지 않으므로 `(type *)0` 은 역참조되지 않고, `ptr` 도 여기서는 평가되지 않는다 → 실제 평가는 뒤쪽 한 번뿐. 비교 대상 타입이 다르면 6.5.9p2 제약 위반이라 컴파일러는 **진단 의무** 가 있다.
- 합법성(6.7.2.1p15, 6.3.2.3p7, 출처(provenance) 논쟁)과 Linux 의 `typeof` + `__builtin_types_compatible_p` 판 비교는 DS-M01 에 정리돼 있으니 여기서는 반복하지 않는다.

`demo_bad_types.c` 를 `-Werror` **없이** 빌드한 실제 결과 — 무엇이 경고로 끝나고 무엇이 무조건 에러인가:

| `-D` | 실수 | clang 21 결과 | 종료 코드 |
|---|---|---|---|
| `BAD_INT` | `int *` 를 `list_head` 멤버로 | `warning: comparison of distinct pointer types ('int *' and 'struct list_head *')` | 0 (빌드됨!) |
| `BAD_INT_STRICT` | 같은 실수, 강화판 매크로 | `error: static assertion failed ... container_of_strict: member size mismatch` | 1 |
| `BAD_ARRAY` | 멤버로 `ch[0]` 대신 `ch` | `warning: comparison of distinct pointer types ('struct chan *' and 'struct chan (*)[4]')` | 0 |
| `BAD_CONST` | `container_of_const` 결과를 non-const 에 | `warning: initializing 'struct task *' with an expression of type 'const struct task *' discards qualifiers` | 0 |
| `BAD_WRAP` | 다른 멤버 이름, 감싼 타입 | `warning: comparison of distinct pointer types ('struct rx_link *' and 'struct tx_link *')` | 0 |
| (기본) | `container_of(&ct->run, ...)` 로 const 를 non-const 에 | **아무 진단 없음** | 0 |

교훈 두 가지. ① 포터블 C11 검사는 **경고** 다. 레거시 빌드 시스템이 `-Werror` 를 안 쓰면 그냥 지나간다 — 그래서 펌웨어 프로젝트는 최소한 `-Werror=compare-distinct-pointer-types` 를 건다. 크기까지 비교하는 강화판(`container_of_strict`, 구조체 안 `_Static_assert` — C11 6.7.2.1p1 문법이 허용)은 크기가 같은 다른 타입은 못 잡지만 무조건 에러다. ② 평범한 `container_of` 는 const 를 **경고 없이** 지운다 → Part B.

**타입 검사의 사각지대 — 같은 타입의 다른 멤버.** `struct node_host` 에는 `list_node` 가 4개 있다. `container_of(&h.mid, struct node_host, last)` 는 타입이 같으니 통과하고, 결과는 `&h - (72 - 24)` = 48 바이트 앞이다. 테스트가 그 정확한 오답 주소를 고정해 둔다. 이건 매크로로 못 고친다 — **타입 설계** 로 고친다: 멤버마다 껍데기 타입을 씌우면(`struct rx_link { struct list_head l; }`, `struct tx_link {...}`) 같은 실수가 `BAD_WRAP` 처럼 타입 불일치가 된다. Linux 의 `list_head` 는 이렇게까지 하지 않는다 — 이건 "강한 typedef(strong typedef)" 라는 일반 기법이고, 비용은 멤버 접근이 `.l` 한 단계 길어지는 것뿐이다.

| 멤버 (arm64) | offset | `container_of` 결과 | 평범한 캐스트 `(T *)ptr` |
|---|---|---|---|
| `first` | 0 | `&h` | `&h` (우연히 맞음 — 6.7.2.1p15) |
| `mid` | 24 | `&h` | `&h + 24` |
| `nest.deep` | 56 | `&h` | `&h + 56` |
| `last` | 72 | `&h` | `&h + 72` |

스텁의 `container_of` 가 바로 이 "평범한 캐스트" 라서 `first` 테스트만 통과한다.

### Part B-1 — const 를 보존하는 container_of

```svg
<svg viewBox="0 0 740 220" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">container_of_const: 한정자를 조건 연산자로 "측정" 하고 _Generic 으로 가지 선택</text>
  <rect x="10" y="50" width="190" height="56" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="72" fill="currentColor" font-family="monospace">ptr: const node *</text>
  <text x="20" y="94" fill="currentColor" font-family="monospace">ptr: node *</text>
  <rect x="240" y="50" width="220" height="56" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="250" y="72" fill="currentColor" font-family="monospace">1 ? (ptr) : (void *)(ptr)</text>
  <text x="250" y="94" fill="currentColor">6.5.15p6: 한정자 합집합</text>
  <rect x="500" y="40" width="230" height="34" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="510" y="62" fill="currentColor" font-family="monospace">const void * → const T *</text>
  <rect x="500" y="84" width="230" height="34" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="510" y="106" fill="currentColor" font-family="monospace">void * → default: T *</text>
  <g stroke="currentColor" fill="none">
    <path d="M200 78 L240 78 M240 78 l-8 -4 M240 78 l-8 4"/>
    <path d="M460 70 L500 57 M500 57 l-9 -1 M500 57 l-5 7"/>
    <path d="M460 86 L500 101 M500 101 l-9 1 M500 101 l-5 -7"/>
  </g>
  <g fill="currentColor">
    <text x="10" y="146">제어식은 평가되지 않는다 (6.5.1.1p3) → ptr 평가는 선택된 가지에서 1회.</text>
    <text x="10" y="168">선택 안 된 가지도 컴파일은 된다 → 두 가지 모두 경고 없는 식이어야 한다.</text>
    <text x="10" y="190">volatile 을 넣으면 volatile void * → default 로 간다 (필요하면 가지를 더 추가).</text>
    <text x="10" y="212">Linux 판은 typeof(*(ptr)) 로 같은 일을 한다 — C11 엔 typeof 가 없어서 이 트릭.</text>
  </g>
</svg>
```

```c
#define QUAL_VOID_PTR_(p) (1 ? (p) : (void *)(p))
#define container_of_const(ptr, type, member)                                                  \
    _Generic(QUAL_VOID_PTR_(ptr),                                                              \
        const void *: ((void)sizeof((ptr) == &((type *)0)->member),                            \
                       (const type *)(const void *)((const char *)(ptr) - MY_OFFSETOF(type, member))), \
        default: container_of(ptr, type, member))
```

- `(void *)(p)` 는 널 포인터 상수가 아니다 (널 포인터 상수는 "값 0 인 정수 상수식, 또는 그걸 `void *` 로 캐스트한 것" — 6.3.2.3p3). 그래서 6.5.15p6 의 "한쪽이 `void *`" 규칙이 적용되고, 결과는 `p` 쪽의 `const` 를 물려받은 `const void *` 다. 만약 `(void *)0` 을 썼다면 결과 타입이 그냥 `p` 의 타입이 돼 `_Generic` 이 `const T *` 이름을 알아야 했을 것이다 — 그걸 모르기 때문에 `void *` 로 "정규화" 하는 것이다.
- const 가지에도 타입 검사를 넣었다: `const node *` 와 `node *` 비교는 한정자만 다르므로 6.5.9p2 상 합법 → 경고 없음.
- 테스트는 같은 트릭으로 결과를 검사한다: `IS_CONST_PTR(container_of_const(&ch->mid, ...)) == 1`, 평범한 판은 0.
- Linux 커널도 2022년 말 `container_of_const()` 를 추가했다 (`_Generic` + `typeof`). 실무 설계 판단: **const 객체에서 주인을 찾는 getter**(`dev_ops.id(const struct device *)`, 추가 연습 3)가 있다면 const 판을 쓰고, 안 그러면 const 가 매크로 하나에서 조용히 사라져 "읽기 전용" 계약이 깨진다. `-Wcast-qual` 을 켜면 이 레슨의 `(void *)(p)` 와 `(char *)(ptr)` 캐스트도 경고 대상이 되니, 켤 프로젝트라면 매크로 안을 `(const char *)` 계열로 맞추거나 그 경고를 매크로 정의부에서만 끈다.

### Part B-2 — NULL 안전, 그리고 인자 1회 평가

`container_of(NULL, T, m)` 은 `NULL` 이 아니라 `(char *)0 - off` 다 — 그 자체가 UB 인 포인터 산술이고, 실제로는 Cortex-M 에서 `0xFFFFFFF8` 같은 주소가 된다. `if (p == NULL)` 검사가 그 뒤에 있으면 통과해 버린다. 반대로 Cortex-M 에서는 0번지가 보통 벡터 테이블(플래시)이라 **NULL 역참조가 폴트 없이 값을 읽기도** 한다 — 그래서 MCU 에서는 "NULL 이면 곧 크래시" 를 믿으면 안 되고, MPU 로 0번지 근처를 막아 두는 프로젝트도 있다.

```c
static inline void *cof_sub_or_null_(void *p, size_t off) {
    return p ? (void *)((char *)p - off) : NULL;
}
#define container_of_or_null(ptr, type, member)                                                \
    ((void)sizeof((ptr) == &((type *)0)->member),                                              \
     (type *)cof_sub_or_null_((ptr), MY_OFFSETOF(type, member)))
```

`p ? p - off : NULL` 을 매크로에 직접 쓰면 `ptr` 이 두 번 평가된다 (`container_of_or_null(next_node(&it), ...)` → `next_node` 두 번 호출 — C-B15 의 매크로 함정). GNU C 라면 문장 표현식 `({ __typeof__(ptr) p_ = (ptr); ... })` 으로 풀겠지만, 순수 C11 에서는 **값을 함수 인자로 한 번 넘기는 것** 이 정석이다 (C-M11). 비용은 없다 — `demo_m4.c` 의 `req_from_desc` 실측:

```text
@ Cortex-M4 -O2                         ; arm64 -O2
req_from_desc:                          _req_from_desc:
    cmp   r0, #0                            sub   x8, x0, #8
    it    ne                                cmp   x0, #0
    subne r0, #8                            csel  x0, xzr, x8, eq
    bx    lr                                ret
```

분기 없이 조건 실행(`it ne` / `csel`) 한 번이다. 이 함수는 non-const 포인터만 받는다 (const 를 넣으면 `discards qualifiers` 경고 → `-Werror` 에서 에러). const 판이 필요하면 Part B-1 처럼 `_Generic` 으로 두 헬퍼를 고른다.

### Part B-3 — 배열 원소 멤버에서 바깥으로

| 방법 | 코드 | 근거·주의 |
|---|---|---|
| ① 첫 원소로 옮긴 뒤 | `container_of(c - c->idx, struct dma_ctrl, ch[0])` | `c - idx` 는 같은 배열 안 산술 (6.5.6p8). 멤버 지정자는 상수 → 표준 `offsetof` 로도 가능 |
| ② 런타임 인덱스 멤버 | `container_of(c, struct dma_ctrl, ch[c->idx])` | `MY_OFFSETOF` 는 런타임 식이라 문제없음. **표준 `offsetof(T, ch[i])` 는 표준 밖** — 7.19p3 은 `&(t.member-designator)` 가 주소 상수일 것을 요구하는데 `i` 가 변수면 아니다. clang 은 `-pedantic` 에서도 조용히 받아 줬다 (이 레슨 실험) |
| ✗ 배열 전체 | `container_of(c, struct dma_ctrl, ch)` | `&((T *)0)->ch` 는 `struct dma_chan (*)[4]` → 타입 검사 경고 (`BAD_ARRAY`). `c` 가 `ch[2]` 면 결과는 2×8 B 어긋남 |

두 방법은 같은 기계어가 된다 (`demo_m4.c` 의 `uart_from_req` vs `uart_from_req_idx`, 실제 출력):

```text
@ Cortex-M4 -O2: uart_from_req (방법 ①)   uart_from_req_idx (방법 ②)
    ldrb  r1, [r0]            ; idx          ldrb  r1, [r0]
    rsbs  r1, r1, #0          ; -idx         sub.w r1, r1, r1, lsl #2      ; idx - 4*idx = -3*idx
    add.w r1, r1, r1, lsl #1  ; -3*idx       add.w r0, r0, r1, lsl #3      ; r - 24*idx
    add.w r0, r0, r1, lsl #3  ; r - 24*idx   subs  r0, #52
    subs  r0, #52             ; - offsetof(tx)
; arm64 -O2: 두 함수 모두
    ldrb  w8, [x0]
    sub   x8, x0, x8, lsl #5     ; r - 32*idx   (arm64 에선 sizeof(tx_req) = 32)
    sub   x0, x8, #56
```

`idx` 를 저장하는 대신 "주인 포인터" 를 저장하는 방법도 있다 (FreeRTOS 의 `ListItem_t.pvOwner` 가 이 방식이다). 비교:

| | 인덱스 저장 + container_of | 주인 포인터 저장 (`void *owner`) |
|---|---|---|
| 원소당 메모리 | 1 B (`uint8_t idx`) | 4 B (M4) / 8 B (arm64) |
| 타입 안전 | 컴파일 타임 검사 | `void *` → 캐스트 실수 못 잡음 |
| 원소가 다른 컨테이너로 이동 | 불가 (배열 위치가 곧 신원) | 가능 |
| 초기화 실수 | idx 를 틀리게 쓰면 틀린 주인 | owner 를 안 쓰면 NULL |

### Part C — DMA 완료 ISR 과 bottom half

```c
static struct tx_req *req_from_desc(struct dma_desc *d) {
    return container_of_or_null(d, struct tx_req, desc);
}
static struct uart_dev *uart_from_req(struct tx_req *r) {
    return container_of(r - r->idx, struct uart_dev, tx[0]);
}
static struct uart_dev *uart_from_work(struct k_work *w) {
    return container_of(w, struct uart_dev, tx_work);
}
```

흐름 (시나리오 1, uart1 의 tx[2]):

| 단계 | 누가 | 무엇을 | 테스트가 보는 것 |
|---|---|---|---|
| 1 | 드라이버 | `uart_tx_start`: `state = BUSY`, `desc.ctrl = OWN` | |
| 2 | DMA 엔진 | 전송 끝 → `OWN` 클리어, `CURR_DESC = &tx[2].desc`, IRQ | |
| 3 | ISR | `d → req → dev`, `state = DONE`, `tx_done++`, `work_submit` | `uart1.tx_done == 1`, `uart0.tx_done == 0`, `pending` |
| 4 | 워크큐 | `handler(&uart1.tx_work)` | `work_run_all() == 1` |
| 5 | bottom half | `w → dev`, DONE 인 요청 회수, cookie 기록, `FREE` | `done_cookie[0] == 0xC0FFEE01` |

시나리오 2 는 ISR 이 두 번 `work_submit` 해도 `pending` 이라 큐에 한 번만 들어가고, bottom half 는 한 번 돌며 DONE 두 개를 **인덱스 순서**(tx[1], tx[3] → `0xA1, 0xA3`)로 회수한다. 이 "합쳐짐" 은 Zephyr `k_work_submit()` 의 의미와 같고, C-M14 의 펜딩 비트 합쳐짐과 같은 이유로 **bottom half 는 "몇 번 불렸나" 가 아니라 "상태" 를 보고 일해야 한다.** 시나리오 4 는 `CURR_DESC = 0` → `req_from_desc` 가 `NULL` → `g_spurious++`. 평범한 `container_of` 였다면 `0 - 8` 주소의 `tx_req` 에 `state = DONE` 을 쓰러 갔을 것이다.

`uart_from_work` 의 기계어는 M4 `subs r0, #148`, arm64 `sub x0, x0, #184` 한 줄이다 (`demo_m4.c` 실측). 콜백 API 를 `void (*cb)(void *ctx)` 로 만들었다면 등록할 때 `ctx` 를 저장할 4 B 가 더 들고, 받는 쪽은 `void *` 를 아무 타입으로나 캐스트할 수 있다.

**실제 칩에서 더 필요한 것 (면접 꼬리질문 재료):**

| 주제 | 이 레슨의 단순화 | 실제 |
|---|---|---|
| 하드웨어 주소 검증 | 0 만 걸러냄 | `CURR_DESC` 는 신뢰할 수 없는 입력. 레지스터 값이 깨지면 container_of 가 엉뚱한 주소를 만들고 HardFault 는 한참 뒤에 난다. 링 범위 안인지, `(addr - ring_base) % sizeof(tx_req) == offsetof(tx_req, desc)` 인지 검사 후 변환 |
| 버스 주소 ≠ CPU 주소 | 같다고 가정 (Cortex-M 은 보통 같음) | Linux/IOMMU/일부 SoC 에선 DMA 가 보는 주소(`dma_addr_t`)가 CPU 가상 주소와 다르다 → container_of 를 바로 못 씀. `idx = (bus - ring_bus) / sizeof(desc)` 로 인덱스를 구해 CPU 쪽 배열에서 찾는다 |
| 캐시 | 없음 | Cortex-M7 D-cache: 디스크립터를 non-cacheable 영역(MPU)에 두거나 clean/invalidate (C-A02 배리어, C-A05 정렬 할당) |
| `work_submit` 동시성 | 동기 호출 | ISR 과 main 이 큐를 같이 만짐 → 임계 구역 필요 (C-M14) |
| 디스크립터 레이아웃 | 그냥 구조체 | 하드웨어가 정한 레이아웃 → `_Static_assert(offsetof/sizeof)` 로 고정 (C-B12, C-A08), `next` 는 32비트 주소 필드 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution    # 42/42 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 42/42, 보고 없음
cc -std=c11 -Wall -Wextra -Werror -pedantic -O2 solution.c -o sol_ped     # -pedantic 에서도 경고 0
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `#define my_offsetof(T,m) ((size_t)&((T *)0)->m)` 를 "표준적" 이라고 답함 | 널 포인터로 멤버 접근 = UB, ICE 도 아님. UBSan 이 런타임에 보고 | 사용자 코드는 `<stddef.h>` offsetof. 금지면 실제 저장소 기준 |
| `my_offsetof` 결과를 `_Static_assert` 에 씀 | clang 은 조용히 접어 주지만 `-pedantic` 에서 `-Wgnu-folding-constant` — 표준이 보장하지 않는 확장(6.6p10)에 기대는 것 | 컴파일 타임 계약에는 표준 offsetof |
| `(T *)((char *)ptr - off)` 대신 `(T *)(ptr - off)` | `ptr` 이 `struct list_node *` 면 `off * sizeof(list_node)` 바이트를 뺀다 | 반드시 `char *` 로 바꾼 뒤 뺀다 |
| 첫 멤버라고 캐스트로 끝냄 | 나중에 누가 앞에 필드를 추가하면 조용히 깨짐 | 항상 container_of (offset 0 이면 어차피 0 바이트 빼기) |
| `container_of(c, struct dma_ctrl, ch)` | 배열 전체 타입 → 경고, 무시하면 `idx * sizeof(chan)` 어긋남 | `container_of(c - c->idx, ..., ch[0])` |
| 같은 타입의 다른 멤버 이름 (`all_node` vs `run_node`) | 타입 검사 통과, 주소만 어긋남 | 멤버별 껍데기 타입, 리뷰 체크리스트, `to_xxx()` 매크로로 멤버 이름을 한 곳에 고정 |
| const 멤버 포인터에 평범한 container_of | const 가 경고 없이 사라짐 | `container_of_const` |
| `container_of(maybe_null, ...)` 후 `if (!p)` | `NULL - off` 는 NULL 이 아님 | `container_of_or_null` 또는 변환 **전에** 검사 |
| `p ? (T *)((char *)p - off) : NULL` 을 매크로에 바로 | `p` 2회 평가 (부작용 2번) | 인라인 함수에 한 번 넘기기 |
| `-Werror` 없는 빌드에서 타입 검사를 믿음 | 포터블 검사는 경고일 뿐 | `-Werror` 또는 `-Werror=compare-distinct-pointer-types`, 크기 검사 강화판 |
| DMA 가 준 주소를 검증 없이 container_of | 깨진 레지스터 값 → 와일드 포인터 → 늦은 HardFault | 링 범위·정렬·멤버 위치 검사 후 변환 |

## 8. 면접 Q&A

<details>
<summary>Q1. offsetof 를 직접 구현해 보세요. 그건 UB 아닌가요?</summary>

고전 답은 `((size_t)&((T *)0)->m)` 이다. 표준상으로는 UB 다: `->` 는 포인터가 가리키는 객체의 멤버를 지정하는데(6.5.2.3p4) 널은 객체를 가리키지 않고, `&*E` 와 달리 `&E->m` 에는 예외 규정이 없다. 포인터→정수 변환도 구현 정의다. 실제로 이 Mac 의 UBSan 은 `member access within null pointer of type 'struct pkt'` 를 보고했다. 그래서 `offsetof` 는 **구현이 제공하는 매크로** 이고, 구현은 자기 컴파일러의 동작을 알기에 그 트릭(또는 `__builtin_offsetof`)을 써도 된다. 사용자 코드에서 직접 해야 한다면 정렬·크기가 충분한 실제 저장소를 기준으로 뺄셈을 한다. 꼬리: "그럼 `_Static_assert` 에 쓸 수 있나?" → 안 된다. 포인터가 낀 식은 ICE 가 아니다 (6.6p6). clang 은 확장으로 접어 주고 `-pedantic` 에서 `-Wgnu-folding-constant` 로 알려 준다.

</details>

<details>
<summary>Q2. container_of 를 설명하고, 왜 합법인지 말해 보세요.</summary>

`(type *)((char *)ptr - offsetof(type, member))`. 멤버는 선언 순서대로 배치되고 맨 앞에는 패딩이 없으며(6.7.2.1p15), 객체 포인터를 `char *` 로 바꾸면 그 객체의 바이트를 훑을 수 있다(6.3.2.3p7). 결과 주소에는 실제로 `type` 객체가 있으므로 그걸로 읽고 쓰는 건 유효 타입 규칙(6.5p7)을 어기지 않는다. 엄밀히는 "멤버를 가리키던 포인터에서 멤버 밖으로 나가는 산술" 이 포인터 출처(provenance) 관점에서 논쟁이 있지만, Linux/Zephyr/Windows(`CONTAINING_RECORD`)가 수십 년 의존해 왔고 주요 컴파일러가 지원한다. 진짜 UB 는 ptr 이 실제로는 그 타입 객체 안에 있지 않을 때다 (sentinel 헤드, 다른 멤버 이름, NULL).

</details>

<details>
<summary>Q3. 당신의 container_of 타입 검사는 무엇을 못 잡나요? 어떻게 보완하죠?</summary>

① 같은 타입의 다른 멤버 이름 (`all_node` 를 `run_node` 로) — 타입이 같으니 통과하고 주소만 어긋난다. 멤버마다 껍데기 구조체 타입을 씌우면 타입 불일치가 된다. ② 포터블 C11 판은 **경고** 다 — `-Werror` 없는 빌드에선 지나간다. 크기를 비교하는 `_Static_assert` 를 구조체 선언 안에 넣은 강화판은 무조건 에러지만 크기가 같은 다른 타입은 못 잡는다. GNU C 에선 `__builtin_types_compatible_p` + `static_assert` (Linux 판)가 정확하다. ③ const 를 조용히 지운다 → `container_of_const`. ④ NULL → `container_of_or_null`. 실무에선 멤버 이름을 `#define to_uart_dev(w) container_of(w, struct uart_dev, tx_work)` 같은 접근자 한 곳에 모아 오타 가능성 자체를 줄인다.

</details>

<details>
<summary>Q4. const struct device * 를 받는 콜백에서 주인 구조체를 찾으면 어떻게 되나요?</summary>

평범한 `container_of` 는 `(char *)` 캐스트에서 const 를 떨어뜨려 `struct foo *` 를 돌려주고, 경고도 없다 (이 레슨 실측: 기본 빌드 진단 0). 그 포인터로 쓰면 원래 const 로 정의된 객체였을 때 UB 이고(6.7.3p6), 아니더라도 "이 함수는 읽기만 한다" 는 API 계약이 깨진다. `_Generic` 으로 const 여부에 따라 `const type *` 를 돌려주는 `container_of_const` 를 쓴다. C11 에선 `typeof` 가 없으니 `1 ? (p) : (void *)(p)` 로 한정자를 `void *` 에 옮겨 `_Generic(..., const void *: ..., default: ...)` 로 고른다 (6.5.15p6). Linux 는 2022년 말 같은 목적의 `container_of_const()` 를 추가했다 (`typeof` 사용).

</details>

<details>
<summary>Q5. container_of 를 쓰면 런타임 비용이 얼마나 되나요? 콜백에 void *ctx 를 넘기는 것과 비교하면?</summary>

상수 오프셋이면 명령어 1개다: Cortex-M4 `subs r0, #148`, arm64 `sub x0, x0, #184` (실측). 배열 인덱스가 끼면 `ldrb` + 곱셈 대신 시프트·덧셈 3개 정도. `void *ctx` 방식은 등록 구조체마다 포인터 하나(M4 4 B)를 더 저장하고, 받는 쪽은 `void *` 를 아무 타입으로나 캐스트할 수 있어 타입 안전이 없다. 대신 ctx 는 "주인이 콜백 구조체를 품고 있지 않아도" 된다 (예: 같은 콜백을 여러 객체에 공유, 콜백 구조체가 ROM 의 const 테이블). 설계 기준: 객체가 콜백/노드를 **소유(내장)** 하면 container_of, 연결이 느슨하거나 동적이면 ctx. STM32 HAL 의 `HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)` 도 `huart` 를 자기 드라이버 구조체 안에 내장해 두면 container_of 로 깔끔하게 주인을 찾을 수 있다.

</details>

<details>
<summary>Q6. DMA 엔진이 완료된 디스크립터 주소를 레지스터로 알려줍니다. 바로 container_of 해도 되나요?</summary>

세 가지를 확인한다. ① **주소 공간**: Cortex-M 처럼 CPU 와 DMA 가 같은 물리 주소를 보면 OK. MMU/IOMMU 가 있으면 레지스터 값은 버스 주소(`dma_addr_t`)라 CPU 포인터가 아니다 → 링의 버스 기준 주소에서 인덱스를 계산해 CPU 쪽 배열에서 찾는다. ② **검증**: 하드웨어 값은 입력이다. 0, 링 범위 밖, 디스크립터 경계가 아닌 값이면 container_of 전에 거른다 (그렇지 않으면 엉뚱한 곳에 `state = DONE` 을 쓰고 한참 뒤 HardFault). ③ **캐시/순서**: Cortex-M7 에선 디스크립터를 non-cacheable 로 두거나 CPU 가 읽기 전에 invalidate, `OWN` 비트를 넘기기 전 `DMB`/`DSB` (C-A02). 이 레슨은 ①을 같다고 가정하고 ②의 NULL 만 처리했다.

</details>

<details>
<summary>Q7. offsetof(struct ctrl, ch[i]) 처럼 런타임 인덱스를 써도 되나요?</summary>

표준으로는 아니다. 7.19p3 은 `static type t;` 일 때 `&(t.member-designator)` 가 **주소 상수** 여야 한다고 요구하는데, `i` 가 변수면 주소 상수가 아니다. GCC/Clang 은 확장으로 받아 준다 (이 레슨 실험에서 clang 은 `-pedantic` 에서도 경고가 없었다) — 그래서 이식성을 원하면 `offsetof(T, ch[0]) + i * sizeof(T.ch[0])` 또는 `c - c->idx` 로 첫 원소에 맞춘 뒤 상수 멤버를 쓰는 편이 안전하다. 이 레슨의 `MY_OFFSETOF` 는 애초에 런타임 식이라 `ch[i]` 가 자연스럽게 된다. 두 방법은 같은 기계어가 나왔다 (5. 풀이 B-3).

</details>

<details>
<summary>Q8. C++ 에서도 offsetof / container_of 를 쓸 수 있나요?</summary>

`offsetof` 는 표준 레이아웃(standard-layout) 클래스에서만 보장되고, 그 밖(가상 함수, 가상 상속, 접근 지정자가 섞인 멤버 등)은 C++17 기준 "조건부 지원" 이다. 그래서 C++ 에서는 침입형 컨테이너를 포인터-투-멤버(`T::*`)를 템플릿 인자로 받아 구현하거나 (Boost.Intrusive 방식), 노드를 **기반 클래스** 로 두고 `static_cast` 로 내려간다 (CRTP — 오프셋 계산을 컴파일러가 대신한다). C 코드를 C++ 로 가져갈 때 `container_of` 매크로가 있는 헤더는 구조체가 표준 레이아웃인지 `static_assert(std::is_standard_layout_v<T>)` 로 먼저 고정하는 게 안전하다.

</details>

## 9. 추가 연습

### 연습 1 — sizeof_field / offsetofend 와 "구멍 없는 헤더"

Linux 의 `sizeof_field(T, m)` 과 `offsetofend(T, m)` 을 만들고, 와이어 헤더 `struct wire_hdr {u8 ver; u8 type; u16 len; u32 seq; u32 crc;}` 의 모든 필드가 빈틈없이 이어지는지 **컴파일 타임** 에 고정하라. `struct bad_hdr {u8 ver; u32 seq; u16 len;}` 의 패딩 바이트 수도 구하라.

<details>
<summary>답 보기</summary>

`sizeof_field(T, m)` = `sizeof(((T *)0)->m)` — `sizeof` 피연산자는 평가되지 않으니 널이어도 안전하고, 결과는 ICE 다. `offsetofend(T, m)` = `offsetof(T, m) + sizeof_field(T, m)`. 표준 `offsetof` 를 쓰므로 `_Static_assert(offsetofend(T, a) == offsetof(T, b))` 가 된다 — `MY_OFFSETOF` 판(`MY_OFFSETOFEND`)은 런타임 검사에만 쓴다 (Part A-1 의 결론 그대로). `bad_hdr` 는 `ver` 뒤 3 B + 꼬리 2 B = 5 B 패딩 (`sizeof` 12 − 필드 합 7). 와이어 포맷은 이렇게 구조체로 고정하기보다 명시적 직렬화가 정석이다 (C-M05).

</details>

### 연습 2 — 소프트 타이머: 서로 다른 주인 두 종류

`struct soft_timer { next; expiry; period; void (*cb)(struct soft_timer *); }` 를 LED 깜빡이(`struct led_blinker`)와 워치독 리로더(`struct wdg_kicker`)에 각각 내장하라. 타이머 서비스는 주인 타입을 모른다. 3 tick / 5 tick 주기로 15 tick 돌렸을 때 토글 5번, 리로드 3번이 돼야 한다.

<details>
<summary>답 보기</summary>

콜백마다 자기 주인 타입으로 `container_of(t, struct led_blinker, tmr)` / `container_of(t, struct wdg_kicker, tmr)`. 서비스 코드는 하나, 주인 타입은 무한히 늘릴 수 있다 — Zephyr `k_timer`, Linux `timer_list`(`from_timer()` 가 바로 container_of 래퍼) 구조다. 만료 비교는 `(int32_t)(now - expiry) >= 0` 으로 해야 32비트 tick 이 넘쳐도(49.7일 @1 kHz) 맞다. 워치독 리로드 키 `0xAAAA` 는 STM32 IWDG_KR 의 값이다.

</details>

### 연습 3 — 드라이버 "상속": to_xxx() 와 ops 테이블

`struct device { name; const struct dev_ops *ops; reads; }` 를 `struct i2c_sensor` 와 `struct spi_flash` 안에 **첫 멤버가 아닌 위치** 로 내장하고, 범용 `dev_read(struct device *)` 가 ops 로 디스패치하게 하라. `id(const struct device *)` 는 const 를 지켜야 한다.

<details>
<summary>답 보기</summary>

`#define to_i2c_sensor(d) container_of(d, struct i2c_sensor, dev)` 와 const 판 `to_i2c_sensor_const(d)` (`container_of_const`). Linux 의 `to_platform_device()`, `to_i2c_client()` 가 정확히 이 모양이다. 멤버 이름(`dev`)이 매크로 한 곳에만 있어서 Q3 의 "다른 멤버 이름" 실수를 줄인다. `id` 콜백이 `const struct device *` 를 받으므로 `container_of_const` 가 `const struct spi_flash *` 를 돌려주고, 거기서 `jedec_id` 를 읽기만 한다 — 평범한 판이었다면 const 가 조용히 사라졌을 것이다.

</details>

### 연습 4 — offset 을 런타임 값으로 들고 다니는 범용 큐

타입별 매크로 없이 하나의 큐 코드로 `struct pkt` 와 `struct event` 를 담아라. 큐는 초기화할 때 "객체 안 노드 위치" 를 받는다.

<details>
<summary>답 보기</summary>

`struct iqueue { head, tail; size_t off; }`. `push(obj)` 는 `(char *)obj + off` 를 노드로, `pop()` 은 `(char *)node - off` 를 객체로 돌려준다 — 오프셋이 컴파일 타임 상수가 아니라 **필드에 저장된 값** 인 container_of 다. 대가: 타입 검사가 전혀 없다 (`void *`), 오프셋 레지스터 로드 1번. 장점: 큐 코드가 한 벌이라 코드 크기가 작고 라이브러리로 배포하기 쉽다. 초기화에 `offsetof` 든 `MY_OFFSETOF` 든 같은 값이 들어간다 (테스트가 둘 다 쓴다).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 12/12 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_san && ./pr_san   # 12/12, 보고 없음
```

## 10. 핵심 정리

- `offsetof` = 멤버 주소 − 기준 주소. 고전 `((size_t)&((T *)0)->m)` 은 **UB**(널로 멤버 접근)이고 **ICE 도 아니다**(6.6p6) — 구현만 쓸 수 있는 트릭이라 `offsetof` 가 라이브러리 매크로다. UBSan: `member access within null pointer`.
- 사용자 코드의 대안: 정렬·크기가 충분한 실제 저장소 기준 뺄셈 + `sizeof(char[ok ? 1 : -1])` 로 크기/정렬 계약을 **무조건 에러** 로. 컴파일 타임 계약(`_Static_assert`)에는 표준 offsetof.
- `container_of(p, T, m)` = `(T *)(void *)((char *)p - offsetof(T, m))` + `(void)sizeof(p == &((T *)0)->m)` 타입 검사. 비용 명령어 1개 (M4 `subs r0, #148`).
- 검사가 못 잡는 것: 같은 타입의 다른 멤버(→ 껍데기 타입, `to_xxx()` 접근자), `-Werror` 없는 빌드(포터블 검사는 경고), const 소실(→ `container_of_const`: `_Generic(1 ? p : (void *)p, const void *: ...)`), NULL(→ `container_of_or_null`, 인자 1회 평가는 인라인 함수로).
- 배열 원소: `container_of(c - c->idx, T, ch[0])`. `ch` 전체를 주면 타입 경고, 표준 `offsetof(T, ch[i])` 런타임 i 는 표준 밖(7.19p3).
- 펌웨어: 하드웨어가 준 주소는 검증 후 변환, 버스 주소 ≠ CPU 주소면 인덱스로, 캐시·배리어·임계 구역은 따로. 콜백 설계: 내장이면 container_of, 느슨하면 `void *ctx`.

## 참고자료

- C11 (N1570): 6.3.2.3 (널 포인터 상수 p3, 포인터↔정수 p6, 정렬·char 접근 p7), 6.5.1.1 (`_Generic`, 제어식 미평가 p3), 6.5.2.3p4 (`->`), 6.5.3.2p3–4 (`&`, `*`), 6.5.3.4p2 (`sizeof` 미평가), 6.5.6p8–9 (포인터 산술·뺄셈), 6.5.9p2 (동등 비교 제약), 6.5.15p6 (조건 연산자 한정자), 6.6p6·p10 (정수 상수식, 구현 확장), 6.7.2.1p1·p15 (구조체 안 static_assert, 첫 멤버), 6.7.6.2p1 (배열 크기 > 0), 7.19p3 (`offsetof`) — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- Linux kernel `include/linux/container_of.h` (`container_of`, `container_of_const`), `include/linux/stddef.h` (`sizeof_field`, `offsetofend`) — <https://elixir.bootlin.com/linux/latest/source/include/linux/container_of.h>
- Zephyr `CONTAINER_OF` / Workqueue (`k_work` 핸들러에서 주인 찾기) — <https://docs.zephyrproject.org/latest/kernel/services/threads/workqueue.html>
- Clang `__builtin_offsetof`, UBSan 체크 목록 (`-fsanitize=null`) — <https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html>
- cppreference: `offsetof` (C / C++ 의 표준 레이아웃 조건) — <https://en.cppreference.com/w/c/types/offsetof>
- 관련 레슨: C-B12 (패딩·offsetof 손계산), C-B15·C-M11 (매크로 인자 다중 평가, `_Generic`), C-M12·C-M13 (UB 카탈로그, strict aliasing), C-M14 (ISR ↔ main 공유, 임계 구역), C-A02 (DMA 배리어), C-A05 (DMA 버퍼 정렬), C-A08 (레이아웃 `_Static_assert`), DS-M01 (침입형 리스트, Linux 판 타입 검사), DS-A05 (침입형 rbtree)
