---
id: C-B05
time: 50분
updated: 2026-11-07
---
# C-B05 포인터 기초와 포인터 산술

## 학습 목표

- `p + 1` 이 **`sizeof(*p)` 바이트** 만큼 움직인다는 규칙으로 `uint8_t*`/`uint32_t*` 주소 증가량을 바로 계산한다.
- `void*` 산술이 **GNU 확장**이라는 것을 알고, 바이트 단위 이동은 `unsigned char*` 또는 `uintptr_t` 로 쓴다.
- **이중 포인터(pointer to pointer)** 로 연결 리스트 head 를 바꾸는 함수와, head 특수 처리 없는 삭제("good taste")를 쓴다.
- 값 swap 과 **포인터 swap** 의 시그니처 차이(`int*` vs `int**`)를 설명한다.
- 레지스터 블록 `base + offset` 을 바이트 단위로 계산하고, 바이트 버퍼 vs 워드 버퍼 순회의 차이를 안다.

## 1. 문제

포인터 산술은 펌웨어 코드 어디에나 있다 — 레지스터 주소 계산, DMA 버퍼 순회, 드라이버의 연결 리스트. 면접에서는 "`(uint32_t*)base + 0x08` 은 몇 번지인가?", "head 를 바꾸려면 왜 `**` 가 필요한가?" 가 단골이다.

핵심 규칙 하나: **포인터에 정수 n을 더하면 주소는 `n × sizeof(가리키는 타입)` 바이트 움직인다** (C11 6.5.6p8). 같은 메모리라도 어떤 타입의 포인터로 보느냐에 따라 `+1` 의 의미가 달라진다.

```svg
<svg viewBox="0 0 720 240" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">같은 16바이트 버퍼, 포인터 타입만 다르다 (buf = {0x11223344, ...}, 리틀 엔디언)</text>
  <g stroke="currentColor" fill="none">
    <rect x="60" y="110" width="576" height="30"/>
    <line x1="96" y1="110" x2="96" y2="140"/><line x1="132" y1="110" x2="132" y2="140"/><line x1="168" y1="110" x2="168" y2="140"/>
    <line x1="204" y1="110" x2="204" y2="140" stroke-width="2.5"/><line x1="240" y1="110" x2="240" y2="140"/><line x1="276" y1="110" x2="276" y2="140"/>
    <line x1="312" y1="110" x2="312" y2="140"/><line x1="348" y1="110" x2="348" y2="140" stroke-width="2.5"/><line x1="384" y1="110" x2="384" y2="140"/>
    <line x1="420" y1="110" x2="420" y2="140"/><line x1="456" y1="110" x2="456" y2="140"/><line x1="492" y1="110" x2="492" y2="140" stroke-width="2.5"/>
    <line x1="528" y1="110" x2="528" y2="140"/><line x1="564" y1="110" x2="564" y2="140"/><line x1="600" y1="110" x2="600" y2="140"/>
  </g>
  <rect x="60" y="110" width="36" height="30" class="f-acc"/>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="78" y="130">44</text><text x="114" y="130">33</text><text x="150" y="130">22</text><text x="186" y="130">11</text>
    <text x="222" y="130">88</text><text x="258" y="130">77</text><text x="294" y="130">66</text><text x="330" y="130">55</text>
    <text x="366" y="130">CC</text><text x="402" y="130">BB</text><text x="438" y="130">AA</text><text x="474" y="130">99</text>
    <text x="510" y="130">00</text><text x="546" y="130">FF</text><text x="582" y="130">EE</text><text x="618" y="130">DD</text>
    <text x="78" y="158">+0</text><text x="222" y="158">+4</text><text x="366" y="158">+8</text><text x="510" y="158">+12</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M78 108 V95 H114 V108"/><path d="M110 102 L114 108 L118 102"/>
    <path d="M78 95 V75 H222 V108"/><path d="M218 102 L222 108 L226 102"/>
    <path d="M78 75 V55 H366 V108"/><path d="M362 102 L366 108 L370 102"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="120" y="92">uint8_t* p+1</text>
    <text x="228" y="72">uint32_t* p+1</text>
    <text x="372" y="52">uint64_t* p+1</text>
  </g>
  <text x="10" y="128" fill="currentColor">p</text>
  <text x="10" y="190" fill="currentColor">주소 증가량 = n × sizeof(*p). 컴파일러가 곱셈을 대신 해 준다 — 그래서 p+1 은 "다음 원소"다.</text>
  <text x="10" y="215" fill="currentColor">void 는 크기가 없어서 표준 C에서 void* + 1 은 제약 위반. 바이트 이동은 (unsigned char*) 나 uintptr_t 로.</text>
</svg>
```

### Part A — 포인터 타입 = 산술 단위

`main()` 은 먼저 같은 버퍼를 `uint8_t*`, `uint16_t*`, `uint32_t*`, `uint64_t*`, `void*`(char* 경유) 로 보고 `p` 와 `p+1` 주소, 증가량을 표로 출력한다(완성되어 있음). 그 다음 세 함수를 구현한다.

- `byte_diff(p, q)` — 같은 객체 안의 두 주소가 몇 **바이트** 떨어져 있는지 (`q - p`, 음수 가능). `void*` 끼리는 뺄 수 없다.
- `byte_advance(p, n)` — `p` 를 n **바이트** 전진한 포인터. `p + n` (void* 산술) 금지.
- `byte_at(words, i)` — `uint32_t` 배열을 바이트 배열로 보고 i번째 바이트를 읽는다.

### Part B — 이중 포인터로 리스트 head 고치기, 포인터 swap

힙 없이 정적 노드 풀(static pool)로 만든 단방향 연결 리스트(singly linked list).

- `list_push_front(struct node **head, struct node *n)` — 맨 앞에 끼운다. 호출자의 `head` 변수 자체가 바뀌어야 한다.
- `list_remove(struct node **head, int val)` — 값이 같은 첫 노드를 떼어내 돌려준다 (`next` 는 NULL 로 정리). 없으면 NULL. **"포인터를 가리키는 포인터"로 head 를 따로 처리하는 `if` 없이** 쓴다.
- `swap_int(int *a, int *b)` — 값 교환. `swap_int(&x, &x)` 도 안전해야 한다.
- `swap_ptr(int **a, int **b)` — 포인터 변수 두 개를 교환 (가리키는 값은 그대로).

### Part C — 레지스터 블록 base + offset, 바이트 vs 워드 버퍼

- `reg_at(base, off)` — 레지스터 블록 시작 주소 `base` 에서 **바이트** offset `off` 에 있는 32비트 레지스터의 주소. 데이터시트 표는 항상 바이트 offset(`0x00, 0x04, 0x08 …`)으로 적혀 있다.
- `xor8(p, len, &iters)` — 바이트 단위로 걸으며 XOR 체크섬. end 포인터 방식 (`while (p != end)`).
- `xor32(p, nwords, &iters)` — 같은 데이터를 32비트 워드 단위로 걸으며 XOR. 루프 횟수가 1/4 이 되는 것과, 접은 결과(`fold32to8`)가 `xor8` 과 같은 것을 확인한다.

## 2. Problem (English)

> **Pointer arithmetic and pointer-to-pointer in C11.**
>
> 1. `ptrdiff_t byte_diff(const void *p, const void *q)` returns the distance in bytes from `p` to `q` (both inside the same object). `const void *byte_advance(const void *p, size_t n)` returns `p` moved forward by `n` bytes. Do **not** use arithmetic on `void *` (it is a GNU extension). `uint8_t byte_at(const uint32_t *words, size_t i)` returns byte `i` of the word array's object representation.
> 2. For `struct node { int val; struct node *next; }`, implement `list_push_front(struct node **head, struct node *n)` and `struct node *list_remove(struct node **head, int val)` that unlinks and returns the first matching node (or `NULL`), with **no special case for the head**. Implement `swap_int(int *a, int *b)` and `swap_ptr(int **a, int **b)`.
> 3. A peripheral register block starts at `base`; the datasheet lists byte offsets `CR=0x00, SR=0x04, DR=0x08, BRR=0x0C`. Implement `volatile uint32_t *reg_at(volatile void *base, uint32_t off)`. Then compute an XOR checksum over a word-aligned DMA buffer twice: byte-by-byte (`xor8`) and word-by-word (`xor32`), reporting the iteration count.
>
> Example: for `uint32_t w[4]`, `byte_diff(w + 3, w) == -12`; `reg_at(base, 0x08)` must equal `base + 8 bytes`, **not** `base + 32 bytes`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 주소 표가 먼저 출력되고, 그 다음 FAIL 줄들과 "5/29 passed" — TODO를 채워 29/29 를 만든다
```

> 맥 SDK 27.2에서 `ld: tapi error` 가 나면 `export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk` 후 다시 빌드.

## 4. 힌트

<details>
<summary>힌트 1 — 바이트 단위로 움직이고 싶으면 바이트 포인터로 바꾼다</summary>

`const unsigned char *c = p;` — `void*` 는 다른 객체 포인터로 **캐스트 없이** 바뀐다 (C11 6.3.2.3p1). 그 다음 `c + n`, `b - a` 는 1바이트 단위다.

</details>

<details>
<summary>힌트 2 — 함수 안에서 호출자의 변수를 바꾸려면 그 변수의 주소가 필요하다</summary>

`int` 를 바꾸려면 `int*`, **`struct node *` 를 바꾸려면 `struct node **`**. head 는 "포인터 변수" 이므로 `&head` 를 넘긴다.

</details>

<details>
<summary>힌트 3 — 노드가 아니라 "링크"를 들고 다닌다</summary>

`struct node **pp` 는 "현재 노드를 가리키고 있는 칸(head 변수 또는 앞 노드의 next 필드)"의 주소다. 시작은 `pp = head`, 전진은 `pp = &(*pp)->next`, 삭제는 `*pp = (*pp)->next`.

</details>

<details>
<summary>힌트 4 — base + offset 은 바이트로 더한 다음 캐스트</summary>

`(volatile uint32_t *)base + off` 는 off 를 **워드 개수**로 해석해 `off × 4` 바이트를 더한다. 먼저 `(volatile uint8_t *)` 로 바꿔 더하고, 그 결과를 `(volatile uint32_t *)` 로 캐스트한다.

</details>

## 5. 풀이

### Part A — `p + n` 은 "n 원소 뒤"

실행해 보면 (Apple Silicon, solution 실행 결과 그대로):

```text
pointer                p              p+1            step
const uint8_t *        0x10446cbd0    0x10446cbd1    1
const uint16_t *       0x10446cbd0    0x10446cbd2    2
const uint32_t *       0x10446cbd0    0x10446cbd4    4
const uint64_t *       0x10446cbd0    0x10446cbd8    8
void * (via char *)    0x10446cbd0    0x10446cbd1    1

29/29 passed
```

(주소는 ASLR 때문에 실행마다 다르다. `nm -m` 으로 보면 `0000000100000bd0 (__TEXT,__const) non-external _main.buf` — 링크 주소 하위 `0xbd0` 이 실행 주소와 같다. ELF/arm-none-eabi 라면 `.rodata` 이고 Cortex-M 에서는 플래시 `0x0800xxxx` 에 놓인다.)

| 연산 | 결과 타입 | 단위 | 표준 |
|---|---|---|---|
| `T *p; p + n` | `T *` | `n × sizeof(T)` 바이트 | 6.5.6p8 — 결과가 같은 배열 안 또는 **끝 바로 다음(one-past-the-end)** 이어야 함 |
| `q - p` (둘 다 `T *`) | `ptrdiff_t` | **원소 개수** | 6.5.6p9 — 같은 배열 안에서만 정의 |
| `void *v; v + 1` | — | — | 6.5.6p2 제약 위반 (void 는 불완전 타입). GCC/clang 은 확장으로 `sizeof(void)==1` 취급 |
| `(unsigned char *)p + n` | `unsigned char *` | 1바이트 | 6.3.2.3p7 — 객체를 바이트 배열로 보는 표준 방법 |
| `(uintptr_t)p + n` | 정수 | 1 (정수 덧셈) | `uintptr_t` 는 선택적 타입(7.20.1.4); 정수→포인터 복원은 **구현 정의** |

그래서 정답은 세 줄짜리다.

```c
const unsigned char *a = p, *b = q;  return b - a;          /* byte_diff   */
const unsigned char *c = p;          return c + n;          /* byte_advance */
const uint8_t *b = (const uint8_t *)words; return b[i];     /* byte_at     */
```

**왜 `byte_at` 은 합법이고 반대 방향은 위험한가?** 문자 타입(`char`/`unsigned char`)으로는 어떤 객체든 읽을 수 있다 (6.5p7 aliasing 예외). 그래서 `uint32_t` 배열 → 바이트 보기는 OK. 반대로 `uint8_t rx[16]` 을 `*(uint32_t *)rx` 로 읽으면 ① 정렬이 안 맞을 수 있고(6.3.2.3p7, Cortex-M0 은 HardFault) ② strict aliasing 위반이다. 이때는 `memcpy` 를 쓴다 (추가 연습 5, 자세한 건 C-M13·C-M04). 엄밀히 표준은 `unsigned char` 만 보장하고 `uint8_t` 는 "사실상 `unsigned char` 의 typedef" 인 구현(gcc/clang 전부)에 기대는 것이다.

`void*` 산술을 실제로 컴파일해 보면 — `demo_void_arith.c`:

```text
$ cc -std=c11 -Wall -Wextra demo_void_arith.c && ./a.out
sizeof(void) = 1, (char*)w - (char*)v = 1                 ← 경고 0개! -Wall -Wextra 로는 안 잡힌다
$ cc -std=c11 -Wpointer-arith demo_void_arith.c
demo_void_arith.c:7:17: warning: arithmetic on a pointer to void is a GNU extension [-Wgnu-pointer-arith]
demo_void_arith.c:9:12: warning: invalid application of 'sizeof' to a void type [-Wpointer-arith]
$ cc -std=c11 -pedantic-errors demo_void_arith.c
demo_void_arith.c:7:17: error: arithmetic on a pointer to void is a GNU extension [-Werror,-Wgnu-pointer-arith]
demo_void_arith.c:9:12: error: invalid application of 'sizeof' to a void type [-Werror,-Wpointer-arith]
```

(Apple clang 21. GCC 도 `-Wpointer-arith` 또는 `-Wpedantic` 에서 경고한다.) Linux 커널은 `-std=gnu11` 이라 `void*` 산술을 일부러 쓰지만, MISRA·이식성이 필요한 펌웨어나 IAR/Keil 같은 다른 컴파일러로 옮길 코드에서는 쓰지 않는다.

### Part B — 이중 포인터: "링크"의 주소를 들고 다닌다

C 는 **값 전달(pass by value)** 만 있다. `void f(struct node *head)` 안에서 `head = n` 을 해도 호출자의 `head` 는 그대로다 — 복사본을 바꿨을 뿐. 그래서 호출자의 포인터 변수를 바꾸려면 그 변수의 주소, 즉 `struct node **` 를 받는다.

```c
n->next = *head;   /* 새 노드가 옛 첫 노드를 가리키고 */
*head = n;         /* 호출자의 head 변수가 새 노드를 가리킨다 */
```

삭제가 더 재밌다. 흔한 교과서 코드는 `prev` 포인터를 들고 다니며 "지울 노드가 head 면 `*head = ...`, 아니면 `prev->next = ...`" 로 **두 갈래**가 된다. Linus Torvalds 가 TED 인터뷰에서 "good taste" 의 예로 든 방법은 노드 대신 **"그 노드를 가리키고 있는 칸의 주소"** 를 들고 다니는 것이다.

```svg
<svg viewBox="0 0 720 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">list_remove(&amp;head, 2) — pp 는 노드가 아니라 "링크 칸"을 가리킨다</text>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="70" width="80" height="36"/>
    <rect x="150" y="70" width="50" height="36"/><rect x="200" y="70" width="70" height="36"/>
    <rect x="330" y="70" width="50" height="36"/><rect x="380" y="70" width="70" height="36"/>
    <rect x="510" y="70" width="50" height="36"/><rect x="560" y="70" width="70" height="36"/>
  </g>
  <rect x="200" y="70" width="70" height="36" class="f-warn"/>
  <rect x="330" y="70" width="120" height="36" class="f-mut"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="60" y="93">head</text>
    <text x="175" y="93">1</text><text x="235" y="93">next</text>
    <text x="355" y="93">2</text><text x="415" y="93">next</text>
    <text x="535" y="93">3</text><text x="595" y="93">NULL</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M80 88 H146"/><path d="M140 84 L148 88 L140 92"/>
    <path d="M250 88 H326"/><path d="M320 84 L328 88 L320 92"/>
    <path d="M430 88 H506"/><path d="M500 84 L508 88 L500 92"/>
    <path d="M235 108 C 270 175, 470 175, 520 110" stroke-dasharray="5 4"/><path d="M512 112 L520 108 L522 117"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="60" y="58">pp=&amp;head</text><text x="235" y="58">pp=&amp;n1.next</text><text x="415" y="58">pp=&amp;n2.next</text>
  </g>
  <text x="300" y="186" fill="currentColor" font-family="monospace" font-size="12">*pp = victim-&gt;next</text>
  <text x="10" y="210" fill="currentColor">val==2 를 찾았을 때 pp 는 n1.next 칸(강조)을 가리킨다. 그 칸에 n2.next 를 쓰면 n2(흐린 칸)가 빠진다.</text>
  <text x="10" y="235" fill="currentColor">지울 게 head 였다면 pp==&amp;head 였을 뿐 — 같은 한 줄로 처리된다. prev 도, if (head) 분기도 없다.</text>
</svg>
```

```c
for (struct node **pp = head; *pp; pp = &(*pp)->next) {
    if ((*pp)->val == val) {
        struct node *victim = *pp;
        *pp = victim->next;    /* 링크 칸을 다음 노드로 건너뛰게 */
        victim->next = NULL;   /* 떼어낸 노드가 리스트를 가리키지 않게 정리 */
        return victim;
    }
}
return NULL;
```

| 식 | 타입 | 뜻 |
|---|---|---|
| `pp` | `struct node **` | 링크 칸의 주소 (`&head` 또는 `&x->next`) |
| `*pp` | `struct node *` | 그 칸에 들어 있는 값 = 현재 노드 |
| `(*pp)->val` | `int` | 현재 노드의 값 (괄호 필수: `*pp->val` 은 `*(pp->val)`) |
| `&(*pp)->next` | `struct node **` | 다음 링크 칸의 주소 (`->` 가 `&` 보다 먼저) |

테스트가 보는 경계: head 제거, tail 제거, 없는 값, 마지막 남은 노드 제거(→ head == NULL), 빈 리스트. `*pp` 가 NULL 이면 루프가 바로 끝나므로 빈 리스트도 따로 처리할 필요가 없다. 노드는 `static` 풀에서 오므로 `free` 하지 않고 돌려준다 — 펌웨어 드라이버(요청 큐, 타이머 리스트)에서 흔한 구조다.

**swap 두 가지.** 바꾸려는 대상의 타입에 `*` 하나를 붙인 것을 받는다.

| 바꾸려는 것 | 받는 타입 | 본문 |
|---|---|---|
| `int` 값 두 개 | `int *a, int *b` | `int t = *a; *a = *b; *b = t;` |
| `int *` 포인터 두 개 | `int **a, int **b` | `int *t = *a; *a = *b; *b = t;` |

`swap_ptr` 후 `pa == &y`, `pb == &x` 지만 `x`, `y` 값은 그대로다. XOR swap(`*a ^= *b; *b ^= *a; *a ^= *b;`)은 `a == b` 면 값이 0이 된다 — 테스트의 `swap_int(&x, &x)` 가 그걸 잡는다.

### Part C — base + offset 은 바이트로

```svg
<svg viewBox="0 0 720 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">base = 0x4001_1000, off = 0x08 (DR) — 포인터 타입에 따라 다른 곳에 도착</text>
  <g stroke="currentColor" fill="none">
    <rect x="40" y="80" width="70" height="34"/><rect x="110" y="80" width="70" height="34"/><rect x="180" y="80" width="70" height="34"/>
    <rect x="250" y="80" width="70" height="34"/><rect x="320" y="80" width="70" height="34"/><rect x="390" y="80" width="70" height="34"/>
    <rect x="460" y="80" width="70" height="34"/><rect x="530" y="80" width="70" height="34"/><rect x="600" y="80" width="70" height="34"/>
  </g>
  <rect x="180" y="80" width="70" height="34" class="f-ok"/>
  <rect x="600" y="80" width="70" height="34" class="f-warn"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="75" y="102">CR</text><text x="145" y="102">SR</text><text x="215" y="102">DR</text><text x="285" y="102">BRR</text>
    <text x="355" y="102">…</text><text x="425" y="102">…</text><text x="495" y="102">…</text><text x="565" y="102">…</text><text x="635" y="102">???</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="75" y="132">+0x00</text><text x="145" y="132">+0x04</text><text x="215" y="132">+0x08</text><text x="285" y="132">+0x0C</text>
    <text x="355" y="132">+0x10</text><text x="425" y="132">+0x14</text><text x="495" y="132">+0x18</text><text x="565" y="132">+0x1C</text><text x="635" y="132">+0x20</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="40" y="52">(uint8_t*)base + 8  → +8 바이트 ✓</text>
    <text x="400" y="52">(uint32_t*)base + 8 → +32 바이트 ✗</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M215 58 V76"/><path d="M211 70 L215 77 L219 70"/>
    <path d="M635 58 V76"/><path d="M631 70 L635 77 L639 70"/>
  </g>
  <text x="10" y="168" fill="currentColor">데이터시트 offset 은 바이트 단위. uint32_t* 에 더하면 ×4 가 한 번 더 붙는다.</text>
  <text x="10" y="190" fill="currentColor">해결: 바이트 포인터로 더한 뒤 캐스트 / off÷4 더하기 / 레지스터 구조체 (C-M01).</text>
</svg>
```

```c
volatile uint8_t *b = base;                 /* volatile void* → volatile uint8_t*, 캐스트 불필요 */
return (volatile uint32_t *)(b + off);      /* 바이트로 더한 뒤 레지스터 타입으로 */
```

실제 주소로 확인 — `demo_reg_offset.c` (STM32F4 USART1, 실제 F4 맵은 `SR=0x00, DR=0x04`; 주소만 계산하고 역참조는 안 함):

```text
$ cc -std=c11 -Wall -Wextra -Werror -O2 demo_reg_offset.c -o demo_reg_offset && ./demo_reg_offset
(uint8_t *)BASE + 0x04        = 0x40011004
(uint32_t*)BASE + 0x04        = 0x40011010   <-- 0x04 * 4 bytes
(uint32_t*)BASE + 0x04 / 4    = 0x40011004
```

- `-Wcast-align` 을 켜면 정답 코드도 경고가 난다 — 컴파일러는 `off` 가 4의 배수인지 모르기 때문 (실측: `warning: cast from 'volatile uint8_t *' (aka 'volatile unsigned char *') to 'volatile uint32_t *' (aka 'volatile unsigned int *') increases required alignment from 1 to 4 [-Wcast-align]`). 정렬 보장은 프로그래머 책임이고, 그래서 벤더 헤더는 구조체 오버레이(`USART1->DR`)를 더 많이 쓴다. 테스트의 `_Static_assert(offsetof(...) == 0x0C)` 가 "구조체 = 데이터시트 맵" 을 컴파일 타임에 고정한다 (offsetof 는 C-B12).
- 실제 칩에서는 `base` 가 정수 상수다: `#define USART1_BASE 0x40011000UL` → `(volatile uint32_t *)(USART1_BASE + 0x04)`. 정수 → 포인터 변환은 **구현 정의**(6.3.2.3p5) 이지만, 평평한 주소 공간의 Cortex-M 컴파일러는 모두 "그 번지" 로 정의한다. 이 경우 정수 덧셈이라 ×4 함정이 없다 — 함정은 **포인터로 캐스트한 다음 더할 때** 생긴다.

**바이트 버퍼 vs 워드 버퍼.** 같은 16바이트를 두 방식으로 걷는다.

| | `xor8` (uint8_t*) | `xor32` (uint32_t*) |
|---|---|---|
| `p++` 한 번 | +1 바이트 | +4 바이트 |
| 루프 횟수 (16바이트) | 16 | 4 |
| 시작 주소 조건 | 아무 주소나 OK (`pkt+1` 테스트) | 4바이트 정렬 필요 (Cortex-M0: 비정렬 LDR → HardFault) |
| 결과 | `0x26` | `0x7A0AE7B1` → `fold32to8` → `0x26` |

XOR 은 비트마다 독립이라 워드 XOR 을 바이트 4개로 접으면 바이트 XOR 과 같다. 펌웨어 체크섬·`memcpy` 최적화가 "앞뒤 자투리는 바이트로, 가운데는 워드로" 걷는 이유가 이것이다. end 포인터 `p + len` 은 one-past-the-end 라 **만드는 것은 합법, 역참조는 불가** — `while (p != end)` 는 역참조 전에 멈춘다.

ASan/UBSan 으로도 확인했다: `cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c && ./a.out` → 보고 없이 `29/29 passed`.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 29/29 passed
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `(volatile uint32_t *)base + 0x08` | offset 을 원소 수로 봐서 `+0x20` 바이트 | `(volatile uint32_t *)((volatile uint8_t *)base + 0x08)` |
| `void *q = p + n;` | void* 산술은 GNU 확장, `-Wall -Wextra` 로 안 잡힘 | `(unsigned char *)p + n`, 빌드에 `-Wpointer-arith` |
| `void push(struct node *head, …) { head = n; }` | 복사본만 바뀜 — 호출자의 head 그대로 | `struct node **head`, `*head = n` |
| `*pp->next` | `->` 가 먼저: `*(pp->next)` — pp 는 구조체 포인터가 아님 → 컴파일 에러 | `(*pp)->next` |
| `void swap_ptr(int *a, int *b) { int *t = a; a = b; b = t; }` | 지역 복사본끼리 교환, 아무 효과 없음 | `int **a, int **b` 와 `*a`, `*b` |
| XOR swap `*a ^= *b; …` | `a == b` 면 0 이 됨, 게다가 더 느릴 수 있음 | 임시 변수 `t` |
| `for (p = end - 1; p >= buf; p--)` | 마지막에 `buf - 1` 을 **계산만 해도 UB** (6.5.6p8) | `for (p = end; p != buf; ) { --p; … }` |
| `*(uint32_t *)rx_bytes` | 비정렬(HardFault 가능) + strict aliasing 위반 | `memcpy(&v, rx_bytes, 4)` (추가 연습 5) |
| `printf("%p", p)` 에 `int *` | `%p` 는 `void *` 를 기대 (7.21.6.1p8) | `printf("%p", (void *)p)` |

## 8. 면접 Q&A

<details>
<summary>Q1. `uint32_t *p = (uint32_t *)0x40000000; p += 3;` 후 p 는? `char *` 였다면?</summary>

`0x4000000C` (3 × 4바이트). `char *` 였다면 `0x40000003`. 꼬리질문 "두 포인터를 빼면?" → 바이트가 아니라 **원소 개수**, 타입은 `ptrdiff_t`. `(uint32_t*)0x4000000C - (uint32_t*)0x40000000 == 3`. (같은 배열 안이어야 정의된다는 것도 덧붙이면 좋다.)

</details>

<details>
<summary>Q2. void* 에 +1 을 하면 어떻게 되나요?</summary>

표준 C 에서는 제약 위반이다 (void 는 불완전 타입이라 크기가 없음, 6.5.6p2). GCC/clang 은 확장으로 `sizeof(void) == 1` 처럼 1바이트 이동하고, `-Wall -Wextra` 로는 경고도 안 나온다 (`-Wpointer-arith`/`-pedantic` 필요). C++ 에서는 에러 (clang++ 실측: `error: arithmetic on a pointer to void`). 이식성 있게 하려면 `(char *)v + 1` 이나 `uintptr_t`. 꼬리질문 "그럼 void* 는 왜 쓰나?" → 타입 모르는 메모리를 주고받는 범용 인터페이스 (`memcpy`, DMA API, 콜백의 `void *ctx`). C 에서는 다른 객체 포인터와 캐스트 없이 오간다.

</details>

<details>
<summary>Q3. 연결 리스트 head 를 바꾸는 함수는 왜 이중 포인터를 받나요? 다른 방법은?</summary>

C 는 값 전달이라 `struct node *head` 를 받으면 복사본만 바뀐다. 방법은 ① `struct node **head` ② 새 head 를 반환 (`head = push(head, n)` — 호출자가 대입을 잊으면 버그) ③ 리스트를 구조체로 감싸 `struct list *` 를 넘김 ④ 더미(sentinel) head 노드 — Linux `list_head` 는 원형 이중 리스트 + sentinel 이라 head 특수 처리가 없다.

</details>

<details>
<summary>Q4. 이중 포인터로 삭제하는 코드를 prev 방식과 비교해 설명해 보세요.</summary>

prev 방식은 "지울 노드가 head 인가?" 분기가 필요하다 (`if (!prev) *head = cur->next; else prev->next = cur->next;`). `struct node **pp` 는 "현재 노드를 가리키는 링크 칸" 의 주소라 head 변수든 앞 노드의 `next` 든 똑같이 `*pp = cur->next` 한 줄. 분기가 사라져 경계 버그가 줄어든다 (Linus 의 "good taste"). 같은 패턴이 정렬 삽입(추가 연습 2), 전부 삭제(추가 연습 1)에도 그대로 쓰인다.

</details>

<details>
<summary>Q5. NULL 을 역참조하면 Cortex-M 에서는 무슨 일이 일어나나요?</summary>

C 에서는 UB. 하지만 Cortex-M 은 주소 0 에 보통 **벡터 테이블**(플래시 alias)이 있어서 읽기가 **조용히 성공**한다 — 초기 MSP 값이 읽힌다. 그래서 리눅스처럼 segfault 로 잡히지 않는다. 대책: MPU 로 0 번지 근처를 no-access 영역으로 설정, 벡터 테이블을 다른 곳으로 재배치(VTOR), 포인터 인자 assert. 꼬리질문 "NULL 은 0 인가?" → 소스의 `0`/`NULL` 은 null pointer constant 이고 (6.3.2.3p3), 실제 비트 패턴이 0 일 필요는 표준상 없다 (현실의 거의 모든 플랫폼은 0).

</details>

<details>
<summary>Q6. `uint8_t` 버퍼를 `uint32_t *` 로 캐스트해 읽어도 되나요?</summary>

두 가지 문제. ① 정렬: 버퍼가 4바이트 정렬이 아니면 6.3.2.3p7 UB, Cortex-M0/M0+ 는 비정렬 LDR 이 HardFault, M3/M4 는 일반 LDR 은 허용하지만 LDM/LDRD 는 폴트. ② strict aliasing: `uint8_t` 배열을 `uint32_t` lvalue 로 접근하는 건 6.5p7 위반이라 `-O2` 에서 컴파일러가 순서를 바꿀 수 있다. 해결은 `memcpy` (컴파일러가 단일 LDR 로 최적화) 또는 바이트 조립 `p[0] | p[1] << 8 …` (엔디언 독립, C-M05). 반대 방향(`uint32_t` 객체를 `unsigned char *` 로 읽기)은 합법이다.

</details>

<details>
<summary>Q7. `int *p;` 와 `int **pp;` 에서 `const` 는 어디에 붙일 수 있고, 레지스터 포인터는 왜 `volatile uint32_t *` 인가요?</summary>

`const int *p` 는 가리키는 값이 읽기 전용, `int *const p` 는 포인터 자체가 고정 (자세한 건 C-B08). `volatile uint32_t *` 는 **가리키는 데이터가 volatile** — 레지스터 읽기/쓰기를 컴파일러가 생략·병합하지 못하게 한다 (C-B09). 포인터 변수 자체를 volatile 로 하려면 `uint32_t *volatile p` 인데, ISR 이 포인터 값을 바꾸는 경우(예: 이중 버퍼 포인터 교체)에 필요하다.

</details>

## 9. 추가 연습

### 연습 1 — 같은 값 전부 삭제

`size_t list_remove_all(struct node **head, int val)` — 지운 개수를 돌려준다. `7 3 7 7 5 7` 에서 7 을 지우면 `3 5`.

<details>
<summary>답 보기</summary>

`pp` 루프에서 **지웠을 때는 `pp` 를 전진시키지 않는다** — `*pp` 에 이미 다음 노드가 들어 있으므로. 지우지 않았을 때만 `pp = &(*pp)->next`. 그래서 `for` 대신 `while (*pp) { if (match) { … } else pp = &(*pp)->next; }`. 연속된 7 (3번째·4번째)에서 실수가 잘 드러난다.

</details>

### 연습 2 — 정렬 유지하며 삽입

`void list_insert_sorted(struct node **head, struct node *n)` — 오름차순 유지. 5, 1, 9, 3 순서로 넣으면 `1 3 5 9`.

<details>
<summary>답 보기</summary>

"끼울 링크 칸" 을 찾는다: `while (*pp && (*pp)->val < n->val) pp = &(*pp)->next;` 그 다음 `n->next = *pp; *pp = n;`. 맨 앞(head 교체)·중간·맨 끝(`*pp == NULL`)이 모두 같은 두 줄. 타이머 리스트를 만료 시각 순으로 유지하는 RTOS 코드가 정확히 이 모양이다.

</details>

### 연습 3 — 리스트 뒤집기

`void list_reverse(struct node **head)` — 빈 리스트도 처리.

<details>
<summary>답 보기</summary>

`prev = NULL, cur = *head` 로 시작해 매 단계 `nx = cur->next; cur->next = prev; prev = cur; cur = nx;`. 끝나면 `*head = prev`. head 가 바뀌므로 이중 포인터(또는 새 head 반환). 면접에서 가장 많이 나오는 리스트 문제 중 하나.

</details>

### 연습 4 — 포인터 정렬 올림 (DMA 버퍼)

`void *align_up(void *p, size_t align)` (align 은 2의 거듭제곱), `bool is_aligned(const void *p, size_t align)`. `pool+1` 을 32 로 올리면 `pool+32`.

<details>
<summary>답 보기</summary>

정수로 바꿔 비트 연산: `a = ((uintptr_t)p + align - 1) & ~(uintptr_t)(align - 1)`. 결과는 `(unsigned char *)p + (a - (uintptr_t)p)` 로 **원래 포인터에서 바이트 전진** 해 만든다 — 정수를 포인터로 직접 바꾸는 것(구현 정의)보다 한 단계 덜 의존적이다. `is_aligned` 는 `((uintptr_t)p & (align - 1)) == 0`. STM32 DMA·캐시 라인(Cortex-M7 32바이트) 정렬에 쓴다 (C-A05).

</details>

### 연습 5 — 비정렬 주소에서 32비트 읽기

`uint32_t load_u32(const void *p)` — 아무 주소에서나 안전하게. 리틀 엔디언에서 `{00 11 22 33 44}` 의 `+1` 에서 읽으면 `0x44332211`.

<details>
<summary>답 보기</summary>

`uint32_t v; memcpy(&v, p, sizeof v); return v;` — `*(const uint32_t *)p` 는 정렬·aliasing 둘 다 위반 가능. clang/gcc 는 이 `memcpy` 를 비정렬 허용 타깃(arm64, Cortex-M4)에서 LDR 한 개로, M0 에서는 바이트 로드 4개로 바꿔 준다. 결과는 호스트 엔디언이라는 점에 주의 (엔디언 독립 버전은 C-M05).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 14/14 passed
```

데모 파일 (경고/주소 관찰용, 하네스 없음):

```bash
cc -std=c11 -Wpointer-arith demo_void_arith.c -o demo_void_arith && ./demo_void_arith     # 경고 2개 확인
cc -std=c11 -Wall -Wextra -Werror -O2 demo_reg_offset.c -o demo_reg_offset && ./demo_reg_offset
```

## 10. 핵심 정리

- `p + n` = `n × sizeof(*p)` 바이트 이동. `q - p` = **원소 개수**(`ptrdiff_t`), 같은 배열 안에서만.
- 배열 끝 바로 다음(one-past-the-end)은 만들 수 있지만 역참조 불가. 그 밖으로 계산만 해도 UB (`buf - 1` 포함).
- `void*` 산술은 GNU 확장 → `(unsigned char *)p + n` 또는 `uintptr_t`. 빌드에 `-Wpointer-arith`.
- 호출자의 `T` 를 바꾸려면 `T *`, 호출자의 **포인터**를 바꾸려면 `T **`. swap 도 같은 규칙.
- 리스트 삭제/삽입은 `struct node **pp` (링크 칸의 주소) 로 → head 특수 처리 없음: `*pp = (*pp)->next`.
- 레지스터 `base + offset` 은 **바이트로 더하고 캐스트**. `(uint32_t *)base + off` 는 ×4 함정.
- 워드 순회는 루프 1/4 이지만 정렬 필요. 바이트 → 다른 타입으로 읽을 땐 `memcpy`.

## 참고자료

- C11 표준 (N1570) 6.5.6 Additive operators — 포인터 산술·one-past-the-end·`ptrdiff_t`; 6.3.2.3 Pointers — void* 변환, null pointer, 정렬; 6.5p7 aliasing 규칙
- [GCC Manual — Arithmetic on void- and Function-Pointers](https://gcc.gnu.org/onlinedocs/gcc/Pointer-Arith.html)
- [Clang Diagnostics — -Wpointer-arith / -Wgnu-pointer-arith](https://clang.llvm.org/docs/DiagnosticsReference.html#wpointer-arith)
- [Linked lists, pointer tricks and good taste (Linus Torvalds TED 2016 예제 해설)](https://github.com/mkirchner/linked-list-good-taste)
- [STM32F4 Reference Manual RM0090 — USART 레지스터 맵 (base 0x40011000)](https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- Kernighan & Ritchie, *The C Programming Language* 2판, 5장 Pointers and Arrays
