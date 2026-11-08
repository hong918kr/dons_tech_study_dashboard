---
id: DS-B01
time: 45분
updated: 2026-11-07
---
# DS-B01 고정 용량 벡터: malloc 없는 동적 배열

## 학습 목표

- 용량을 **컴파일 타임 상수**로 정한 정적 배열 위에 `push / pop / get / set / insert / erase` 를 구현한다.
- 유효 인덱스가 `0..len-1` (insert만 `0..len`) 이라는 **경계 규칙**을 몸에 익히고, `len` 과 `cap` 을 헷갈리지 않는다.
- 겹치는 복사에 **`memmove`** 를 써야 하는 이유와, 원소 이동 비용 O(n) 을 설명한다.
- "가득 참"을 상태 코드·drop-oldest 같은 **정책**으로 다루고, 불변식(invariant)·캐너리(canary)·참조 모델·**ASan** 으로 검증한다.

## 1. 문제

펌웨어는 보통 `malloc` 을 쓰지 않는다 (MISRA C:2012 Rule 21.3, 결정성, 단편화). 그래도 "개수가 변하는 목록"은 계속 필요하다 — 등록된 CAN 필터, 연결된 BLE 피어, SSD의 bad block 목록, 센서 보정 테이블. 그래서 **최대 개수를 미리 정한 배열 + 현재 개수(len)** 조합이 펌웨어의 기본 "동적 배열"이다. 면접에서는 이 구조로 **off-by-one 과 경계 처리**를 얼마나 정확히 하는지를 본다.

C++의 `std::vector` 와 다른 점은 딱 하나다: **자라지 않는다.** 꽉 차면 `realloc` 대신 "거절하거나, 정책대로 버린다".

```svg
<svg viewBox="0 0 720 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b1a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">FV_DEFINE(v, 8) — 정적 배열 8칸, 그중 len 칸만 "살아 있다"</text>
  <text x="10" y="72" fill="currentColor">before</text>
  <text x="10" y="172" fill="currentColor">insert(2, 99)</text>
  <g class="f-acc" stroke="currentColor">
    <rect x="130" y="50" width="54" height="34"/><rect x="184" y="50" width="54" height="34"/><rect x="238" y="50" width="54" height="34"/><rect x="292" y="50" width="54" height="34"/><rect x="346" y="50" width="54" height="34"/>
    <rect x="130" y="150" width="54" height="34"/><rect x="184" y="150" width="54" height="34"/><rect x="292" y="150" width="54" height="34"/><rect x="346" y="150" width="54" height="34"/><rect x="400" y="150" width="54" height="34"/>
  </g>
  <rect x="238" y="150" width="54" height="34" class="f-warn" stroke="currentColor"/>
  <g stroke="currentColor" fill="none">
    <rect x="400" y="50" width="54" height="34"/><rect x="454" y="50" width="54" height="34"/><rect x="508" y="50" width="54" height="34"/>
    <rect x="454" y="150" width="54" height="34"/><rect x="508" y="150" width="54" height="34"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="157" y="72">10</text><text x="211" y="72">20</text><text x="265" y="72">30</text><text x="319" y="72">40</text><text x="373" y="72">50</text>
    <text x="157" y="172">10</text><text x="211" y="172">20</text><text x="265" y="172">99</text><text x="319" y="172">30</text><text x="373" y="172">40</text><text x="427" y="172">50</text>
    <text x="157" y="100" font-size="12">0</text><text x="211" y="100" font-size="12">1</text><text x="265" y="100" font-size="12">2</text><text x="319" y="100" font-size="12">3</text>
    <text x="373" y="100" font-size="12">4</text><text x="427" y="100" font-size="12">5</text><text x="481" y="100" font-size="12">6</text><text x="535" y="100" font-size="12">7</text>
  </g>
  <path d="M373 86 L425 147" stroke="currentColor" fill="none" marker-end="url(#b1a)"/>
  <path d="M319 86 L371 147" stroke="currentColor" fill="none" marker-end="url(#b1a)"/>
  <path d="M265 86 L317 147" stroke="currentColor" fill="none" marker-end="url(#b1a)"/>
  <line x1="400" y1="44" x2="400" y2="90" stroke="currentColor" stroke-dasharray="4 3"/>
  <line x1="454" y1="144" x2="454" y2="190" stroke="currentColor" stroke-dasharray="4 3"/>
  <g fill="currentColor">
    <text x="580" y="72">len=5, cap=8</text>
    <text x="580" y="172">len=6, cap=8</text>
    <text x="10" y="220">insert 는 data[i..len) 을 한 칸씩 뒤로 민 다음 data[i] 에 쓴다 → 최악 O(n). 맨 뒤 push 는 O(1).</text>
    <text x="10" y="245">len == cap 이면 쓰기 전에 거절(FV_ERR_FULL) → data[cap] 은 절대 건드리지 않는다.</text>
  </g>
</svg>
```

구조는 이렇다. 저장소 배열은 `static` 이고, 핸들(`fixed_vec`)은 그 배열을 가리키는 포인터 + `len` + `cap` 이다. 매크로 `FV_DEFINE(name, CAP)` 가 둘을 한 번에 만든다 — Zephyr의 `K_MSGQ_DEFINE`, FreeRTOS의 `xQueueCreateStatic` 과 같은 패턴이다. 장점: 용량이 다른 벡터 여러 개가 **코드 한 벌**을 공유하고, RAM 사용량이 링크 맵(`.bss`)에 그대로 찍힌다.

### Part A — 끝에서 넣고 빼기 + 경계 검사

- `fv_push(v, x)` — 가득 차면 `FV_ERR_FULL`, 아니면 맨 뒤에 넣는다.
- `fv_pop(v, &out)` — 비었으면 `FV_ERR_EMPTY`.
- `fv_get(v, i, &out)`, `fv_set(v, i, x)` — `i` 가 `0..len-1` 밖이면 `FV_ERR_RANGE`. 실패하면 **아무것도 바꾸지 않는다**.

### Part B — 중간 삽입/삭제와 원소 이동

- `fv_insert(v, i, x)` — `i` 는 `0..len` (len이면 맨 뒤). 검사 순서는 **RANGE → FULL**.
- `fv_erase(v, i, &out)` — `i` 는 `0..len-1`. 뒤쪽 원소를 한 칸씩 당겨 구멍을 메운다. `out` 이 `NULL` 이면 값은 버린다.
- 두 함수 모두 순서를 유지해야 하고, 겹치는 구간 복사를 올바르게 해야 한다.

### Part C — 불변식, 가득 참 정책, 검증

- `fv_invariant(v)` — 언제나 참이어야 하는 조건 (`data != NULL`, `cap > 0`, `len <= cap`).
- `fv_push_drop_oldest(v, x)` — "최근 N개 로그" 정책: 가득 차면 맨 앞(가장 오래된 것)을 버리고 넣는다. 버렸으면 `true`.
- 하네스가 이미 준비한 검증 3종을 통과시킨다: ① 경계 테스트 ② **캐너리**로 둘러싼 저장소에 넘치는 연산 퍼붓기 ③ 고정 시드 무작위 연산 20000번을 **참조 모델**과 비교. 그리고 **ASan/UBSan** 빌드로 다시 돌려 깨끗한지 확인한다.

## 2. Problem (English)

> **Fixed-capacity vector without heap allocation.**
>
> Implement a vector of `uint32_t` whose storage is a statically allocated array with a compile-time capacity:
>
> ```c
> typedef enum { FV_OK, FV_ERR_FULL, FV_ERR_EMPTY, FV_ERR_RANGE } fv_status;
> typedef struct { uint32_t *data; size_t len; size_t cap; } fixed_vec;
> #define FV_DEFINE(name, CAP)  /* defines static storage and a handle */
>
> fv_status fv_push  (fixed_vec *v, uint32_t x);
> fv_status fv_pop   (fixed_vec *v, uint32_t *out);
> fv_status fv_get   (const fixed_vec *v, size_t i, uint32_t *out);
> fv_status fv_set   (fixed_vec *v, size_t i, uint32_t x);
> fv_status fv_insert(fixed_vec *v, size_t i, uint32_t x);  /* 0 <= i <= len   */
> fv_status fv_erase (fixed_vec *v, size_t i, uint32_t *out); /* 0 <= i < len  */
> ```
>
> Constraints: no `malloc`/`free`; every failing call must leave the vector unchanged; `insert` reports `FV_ERR_RANGE` before `FV_ERR_FULL`; order of elements must be preserved. Never read or write outside `data[0..cap)`.
>
> Example (cap = 4): push 10, 20, 30 → `[10,20,30]`; `insert(0, 5)` → `[5,10,20,30]`; `insert(2, 99)` → `FV_ERR_FULL`; `erase(1)` → returns 10, `[5,20,30]`; `get(3)` → `FV_ERR_RANGE`.
>
> Follow-up: how would you verify there is no out-of-bounds access? What would you change if `erase` had to be O(1)?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "9/25 passed" — TODO 8개를 채워 25/25 를 만든다
```

> 맥 SDK 27.2에서 `ld: tapi error` 가 나면 `export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk` 후 다시 빌드.

하네스가 찍는 stress 표의 숫자는 **참조 모델** 기준이라 starter에서도 같게 나온다. 여러분의 구현이 모델과 한 번이라도 다르면 `mismatch == 0` 이 FAIL 한다.

## 4. 힌트

<details>
<summary>힌트 1 — 유효 범위는 cap 이 아니라 len 기준</summary>

`get/set/erase` 의 유효 인덱스는 `i < len`. `i < cap` 으로 검사하면 아직 넣지 않은 쓰레기 칸을 읽게 된다 (메모리 오류는 아니라서 ASan도 못 잡는 **논리 버그**). `insert` 만 `i <= len` (맨 뒤 삽입 허용).

</details>

<details>
<summary>힌트 2 — 인덱스 타입이 size_t 면 음수 검사는 공짜</summary>

`size_t` 는 unsigned 라 `-1` 을 넘기면 `SIZE_MAX` 가 된다. 그래서 `i >= len` 하나로 "음수"와 "너무 큼"이 같이 걸린다. `if (i < 0)` 을 쓰면 컴파일러가 "항상 거짓" 경고를 낸다.

</details>

<details>
<summary>힌트 3 — 이동은 memmove, 개수는 len - i (또는 len - i - 1)</summary>

insert: `data[i..len)` 을 `data[i+1..len+1)` 로 → 개수 `len - i`. erase: `data[i+1..len)` 을 `data[i..len-1)` 로 → 개수 `len - i - 1`. 출발지와 목적지가 **겹치므로** `memcpy` 가 아니라 `memmove`. 손으로 루프를 쓴다면 insert는 **뒤에서부터**, erase는 **앞에서부터** 옮겨야 덮어쓰지 않는다.

</details>

<details>
<summary>힌트 4 — 실패하면 아무것도 바꾸지 않는다</summary>

모든 검사를 **먼저** 하고, 통과한 뒤에만 메모리와 `len` 을 바꾼다. 쓰고 나서 검사하면 이미 `data[cap]` 을 밟은 뒤다.

</details>

<details>
<summary>힌트 5 — drop_oldest 는 이미 만든 함수 두 개의 조합</summary>

`if (full) erase(0); push(x);` — 다만 erase(0)은 원소 전체를 당기는 O(n) 이다. 이 정책을 자주 쓸 거라면 링 버퍼(DS-B03)가 정답이다.

</details>

## 5. 풀이

### Part A — "검사 → 접근" 순서와 len/cap 의 역할

| 함수 | 실패 조건 | 성공 시 동작 | 시간 |
|---|---|---|---|
| `push` | `len == cap` → FULL | `data[len++] = x` | O(1) |
| `pop` | `len == 0` → EMPTY | `*out = data[--len]` | O(1) |
| `get` | `i >= len` → RANGE | `*out = data[i]` | O(1) |
| `set` | `i >= len` → RANGE | `data[i] = x` | O(1) |

`data[len++] = x` 는 "현재 len 자리에 쓰고, 그 다음 len 을 1 올린다". `data[--len]` 은 "먼저 len 을 내리고 그 자리를 읽는다". 이 두 줄이 서로 정확히 거꾸로라서 push 후 pop 하면 원상복구된다.

`cap` 은 오직 **push/insert 의 FULL 검사**에만 쓰이고, 읽기 쪽은 전부 `len` 이 기준이다. 이걸 섞으면 "범위 밖은 아닌데 틀린 값"을 읽는 버그가 된다.

### Part B — 겹치는 이동: memmove 와 방향

```svg
<svg viewBox="0 0 720 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b1b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">erase(1) on [5, 11, 20, 30]  (cap = 4)</text>
  <text x="10" y="67" fill="currentColor">before</text>
  <text x="10" y="157" fill="currentColor">after</text>
  <g class="f-acc" stroke="currentColor">
    <rect x="130" y="45" width="60" height="34"/><rect x="250" y="45" width="60" height="34"/><rect x="310" y="45" width="60" height="34"/>
    <rect x="130" y="135" width="60" height="34"/><rect x="190" y="135" width="60" height="34"/><rect x="250" y="135" width="60" height="34"/>
  </g>
  <rect x="190" y="45" width="60" height="34" class="f-warn" stroke="currentColor"/>
  <rect x="310" y="135" width="60" height="34" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="160" y="67">5</text><text x="220" y="67">11</text><text x="280" y="67">20</text><text x="340" y="67">30</text>
    <text x="160" y="157">5</text><text x="220" y="157">20</text><text x="280" y="157">30</text><text x="340" y="157">30</text>
    <text x="340" y="190" font-size="12">쓰레기</text>
  </g>
  <path d="M280 81 L222 132" stroke="currentColor" fill="none" marker-end="url(#b1b)"/>
  <path d="M340 81 L282 132" stroke="currentColor" fill="none" marker-end="url(#b1b)"/>
  <rect x="420" y="40" width="290" height="150" rx="8" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="430" y="62" font-family="monospace">memmove(&amp;d[i], &amp;d[i+1], (len-i-1)*4)</text>
    <text x="430" y="88">옮길 개수 = len-i-1 = 4-1-1 = 2</text>
    <text x="430" y="112">src [2..4) 와 dst [1..3) 가 겹친다</text>
    <text x="430" y="136">→ memcpy 는 UB, memmove 는 안전</text>
    <text x="430" y="160">d[3] 의 30 은 남지만 len=3 이라</text>
    <text x="430" y="180">"없는 원소" — 읽기 API가 막아준다</text>
  </g>
  <text x="10" y="220" fill="currentColor">손 루프라면: erase 는 앞에서부터(j = i..len-2), insert 는 뒤에서부터(j = len..i+1) 옮겨야 덮어쓰지 않는다.</text>
</svg>
```

**방향이 왜 중요한가.** insert를 앞에서부터 `data[j+1] = data[j]` 로 돌리면, `data[i]` 가 `data[i+1]` 을 덮고, 그 값이 또 `data[i+2]` 를 덮어서 전부 같은 값이 된다. `memmove` 는 내부에서 겹침 방향을 보고 알아서 고른다. 그래서 라이브러리 함수를 쓰는 게 가장 안전하고, 보통 워드 단위로 복사해서 손 루프보다 빠르다.

**비용.** 위치 i 에 넣고 빼면 `len - i` 개 정도를 옮긴다. 무작위 위치라면 평균 `len/2`. 아래 stress 결과에서 실제 평균 이동 수가 6.72 이고, 같은 순간들의 평균 len 을 따로 재보면 13.98 (cap 16) 이다 → 6.72 ≈ 13.98 / 2, "대략 len/2" 와 맞는다.

**검사 순서 RANGE → FULL.** 꽉 찬 상태에서 `insert(9, x)` 를 하면 둘 다 틀렸다. 어느 쪽을 먼저 보고할지는 **API 계약**으로 정하면 된다. 이 레슨은 "인덱스가 말이 안 되면 그게 더 근본적인 호출자 버그"라 보고 RANGE 를 먼저 둔다. 중요한 건 정했으면 문서화하고 테스트로 못 박는 것 (하네스의 `fv_insert(&v4, 9, 99) == FV_ERR_RANGE`).

### Part C — 불변식, 정책, 그리고 검증 3층

**불변식.** `fixed_vec` 에서 깨지면 안 되는 사실은 `len <= cap` 하나가 핵심이다. 이게 깨지면 `data[len-1]` 접근이 곧 범위 밖 접근이 된다. 그래서 디버그 빌드에서는 모든 연산 끝에 `assert(fv_invariant(v))` 를 넣고, 하네스도 매 스텝 검사한다.

**가득 참 정책 비교.**

| 정책 | 동작 | 언제 |
|---|---|---|
| 거절 (FULL 반환) | 호출자가 처리 | 설정 테이블, 필터 등록 — 실패를 알려야 함 |
| drop-oldest | 맨 앞 버리고 넣기 | 최근 N개 이벤트 로그, 크래시 직전 기록 |
| drop-newest | 새 것을 버림 (카운터만 증가) | ISR에서 넣는 경우 — 빠르고 안전 |
| assert/panic | 설계상 불가능해야 함 | 용량을 정적으로 증명한 경우 |

**검증 3층.** 각 도구가 잡는 범위가 다르다.

```svg
<svg viewBox="0 0 720 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">캐너리 vs ASan redzone — 서로 못 잡는 것을 잡는다</text>
  <text x="10" y="72" fill="currentColor">캐너리 struct</text>
  <text x="10" y="142" fill="currentColor">ASan 전역 배열</text>
  <rect x="150" y="50" width="90" height="34" class="f-ok" stroke="currentColor"/>
  <rect x="480" y="50" width="90" height="34" class="f-ok" stroke="currentColor"/>
  <g class="f-acc" stroke="currentColor">
    <rect x="240" y="50" width="60" height="34"/><rect x="300" y="50" width="60" height="34"/><rect x="360" y="50" width="60" height="34"/><rect x="420" y="50" width="60" height="34"/>
    <rect x="240" y="120" width="60" height="34"/><rect x="300" y="120" width="60" height="34"/><rect x="360" y="120" width="60" height="34"/><rect x="420" y="120" width="60" height="34"/>
  </g>
  <rect x="480" y="120" width="140" height="34" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="195" y="72">DEADBEEF</text><text x="525" y="72">DEADBEEF</text>
    <text x="270" y="72">buf0</text><text x="330" y="72">buf1</text><text x="390" y="72">buf2</text><text x="450" y="72">buf3</text>
    <text x="270" y="142">s0</text><text x="330" y="142">s1</text><text x="390" y="142">s2</text><text x="450" y="142">s3</text>
    <text x="550" y="142">redzone</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="185">캐너리: 쓰기 넘침을 "검사하는 시점에" 발견. 읽기 넘침은 못 잡는다.</text>
    <text x="10" y="203">  → 타깃 보드에서도 동작 (스택 캐너리, RTOS 스택 검사와 같은 원리).</text>
    <text x="10" y="227">ASan: 읽기·쓰기 넘침을 "그 순간" 잡는다. 호스트 테스트 전용.</text>
    <text x="10" y="245">  → 단 같은 struct 안 필드 사이 넘침(intra-object)은 못 잡는다.</text>
  </g>
</svg>
```

1. **경계 테스트 (손으로 고른 케이스)**: 빈 벡터에서 pop/erase, 꽉 찬 상태에서 push/insert, `i == len`, `i == len+1`, `i == (size_t)-1`. 버그는 거의 다 경계에서 나온다.
2. **캐너리**: 저장소 앞뒤에 `0xDEADBEEF` 를 두고, 꽉 찬 상태에서 넘치는 연산을 퍼부은 뒤 캐너리가 그대로인지 본다. ASan이 못 잡는 "구조체 내부 넘침"을 잡는다 — `guarded.buf[4]` 에 쓰면 그건 `guarded.post` 라서 ASan 입장에선 "합법적인 메모리"다.
3. **참조 모델 + 무작위 연산**: 느리지만 **뻔히 맞는** 구현(임시 배열에 다시 쌓기)과 매 연산 결과·내용을 비교한다. 고정 시드 xorshift32 라 실패하면 같은 순서로 재현된다.

실제 실행 결과 (solution.c, seed `0x12345678`):

| op | OK | FULL | EMPTY | RANGE |
|---|---:|---:|---:|---:|
| push | 3725 | 2296 | 0 | 0 |
| pop | 2976 | 0 | 2 | 0 |
| insert | 2747 | 1897 | 0 | 315 |
| erase | 3480 | 0 | 0 | 513 |
| set | 1800 | 0 | 0 | 249 |

평균 이동 원소 수 (성공한 insert/erase 한 번당) = **6.72**. push 가 pop 보다 많아서 벡터가 대부분 꽉 차 있고, 그래서 FULL 경로가 수천 번 실행됐다 — 경계 경로를 **충분히 많이** 밟았다는 게 이 표의 의미다. EMPTY 가 2번뿐인 건 "빈 상태" 경로가 거의 안 밟혔다는 경고이기도 하다 (그 경로는 손으로 고른 경계 테스트가 보완한다).

마지막으로 ASan + UBSan 빌드:

```bash
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined solution.c -o sol_asan && ./sol_asan
# 25/25 passed, sanitizer 출력 없음
```

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 25/25 passed
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined solution.c -o sol_asan && ./sol_asan
```

## 7. 자주 하는 실수

**① erase 루프의 off-by-one — ASan이 실제로 잡는 모습.** 아래처럼 `j < len` 까지 돌리면 마지막 반복에서 `data[len]` 을 읽는다. `len == cap` 이면 배열 밖이다.

```c
for (size_t j = i; j < len; j++) storage[j] = storage[j + 1];   /* 틀림: j+1 == len 에서 밖을 읽음 */
```

`static uint32_t storage[4] = {1,2,3,4}; len = 4;` 에서 `erase_buggy(1)` 을 부르는 10줄짜리 데모(`oob.c`)로 실제 돌려보면, `-O2` 일반 빌드에서는 조용히 `4` 를 출력하고 끝난다. ASan 빌드에서는 (컴파일러가 루프를 memmove 로 바꿔서 memmove 에서 걸린다):

```text
==9618==ERROR: AddressSanitizer: global-buffer-overflow on address 0x000102b80010 ...
READ of size 12 at 0x000102b80010 thread T0
    #0 0x00010323a798 in __asan_memmove+0x19c
    #1 0x000102b78864 in main oob.c:10
0x000102b80010 is located 0 bytes after global variable 'storage' defined in 'oob.c' (0x000102b80000) of size 16
```

"0 bytes after ... of size 16" — 4칸짜리 배열의 바로 다음을 읽었다는 뜻. 고침: `j + 1 < len` (또는 memmove 개수 `len - i - 1`).

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `if (i < v->cap)` 로 get 검사 | 넣지 않은 칸(쓰레기)을 읽음 — 메모리 오류가 아니라 ASan도 못 잡는 논리 버그 | `if (i >= v->len) return FV_ERR_RANGE;` |
| `if (i <= v->len)` 로 erase 검사 | `i == len` 은 원소가 없는 자리 | erase/get/set 은 `<`, insert 만 `<=` |
| 겹치는 이동에 `memcpy` | 겹침에서 동작이 정의되지 않음 (C11 7.24.2.1) | `memmove` |
| insert 손 루프를 앞에서부터 | `data[i]` 값이 뒤로 계속 복제됨 | `for (j = len; j > i; j--) d[j] = d[j-1];` |
| 쓰고 나서 FULL 검사 | 이미 `data[cap]` 을 밟음 | 검사 먼저, 변경은 나중 |
| `int i` 인덱스 + `if (i < 0)` 없음 | 음수가 `size_t` 로 변환될 때 거대한 수가 되거나, int 비교에선 음수 인덱스로 접근 | 인덱스는 `size_t`, `i >= len` 하나로 거른다 |
| `if (len + n > cap)` | `n` 이 크면 덧셈이 넘쳐 통과 | `if (n > cap - len)` (추가 연습 4) |
| `sizeof v->data / sizeof v->data[0]` 로 용량 계산 | `data` 는 포인터 → 8/4 = 2 | `cap` 필드를 쓰거나 매크로에서 `sizeof name##_storage` |

## 8. 면접 Q&A

<details>
<summary>Q1. 펌웨어에서 malloc 을 피하는 이유는? 그럼 동적인 목록은 어떻게 다루나요?</summary>

① **결정성**: 일반 할당기는 실행 시간이 입력에 따라 달라 실시간 마감을 보장하기 어렵다. ② **단편화**: 수개월 돌아가는 장치에서 총 여유는 충분한데 연속 블록이 없어 실패한다. ③ **실패 처리**: 런타임 할당 실패 경로를 테스트하기 어렵다. ④ **인증/규칙**: MISRA C Rule 21.3, DO-178C/ISO 26262 프로젝트는 금지하거나 초기화 단계에서만 허용. 대신 최대 개수를 설계 때 정해 정적 배열(+len)·메모리 풀(DS-M02)·링 버퍼를 쓴다. 장점: RAM 사용량이 링크 맵에서 **빌드 타임에** 보인다.

</details>

<details>
<summary>Q2. memcpy 와 memmove 의 차이는? 언제 memmove 가 필요한가요?</summary>

`memcpy` 는 출발지·목적지가 겹치지 않는다고 **가정**한다(겹치면 UB). `memmove` 는 겹쳐도 "임시 버퍼에 복사한 것처럼" 동작하도록 방향을 골라 복사한다. 같은 배열 안에서 원소를 미는 insert/erase 는 항상 겹치므로 `memmove`. 꼬리질문 "memmove 를 직접 구현해보라" → `dst < src` 면 앞에서부터, 아니면 뒤에서부터 복사.

</details>

<details>
<summary>Q3. insert/erase 가 O(n) 인데, 더 빠르게 하려면?</summary>

순서가 필요 없으면 **swap-remove** (마지막 원소를 구멍으로 옮김) 로 erase O(1) — 추가 연습 1. 앞에서 빼고 뒤에 넣는 FIFO 패턴이면 **링 버퍼**(DS-B03)로 양쪽 O(1). 정렬된 상태로 찾기가 많으면 정렬 배열 + 이진 탐색(추가 연습 3; 삽입 O(n), 검색 O(log n)) — 원소 수가 수십 개 이하인 펌웨어에선 캐시 지역성 덕분에 트리보다 빠른 경우가 많다.

</details>

<details>
<summary>Q4. 이 벡터를 ISR 과 메인 루프가 같이 쓰면 무엇이 문제인가요?</summary>

`data[len++] = x` 는 "len 읽기 → 쓰기 → len 갱신" 여러 단계라, 메인이 push 하는 중간에 ISR 이 push 하면 같은 칸에 쓰거나 len 갱신을 잃는다 (C-B01 의 RMW 경쟁과 같은 구조). insert/erase 의 memmove 중간에 ISR 이 읽으면 반쯤 옮겨진 배열을 본다. 해결: 짧은 임계 구역(인터럽트 끄기) 으로 감싸거나, 생산자 1·소비자 1 이면 lock-free 가 가능한 **SPSC 링 버퍼**로 구조 자체를 바꾼다. ISR 안에서 O(n) memmove 를 하는 것 자체가 지연 시간 측면에서 좋지 않다.

</details>

<details>
<summary>Q5. 범위 밖 접근이 없다는 걸 어떻게 검증하나요? 타깃(Cortex-M)에서는?</summary>

호스트: 경계 단위 테스트 + 무작위 연산과 참조 모델 비교(property-based) + ASan/UBSan 빌드, 가능하면 fuzzing(libFuzzer). 타깃에는 ASan 이 없으니: 캐너리/가드 워드, **MPU** 로 배열 뒤에 접근 금지 영역 두기, DWT 워치포인트로 특정 주소 쓰기 감시, `assert` 로 불변식 검사. 즉 "같은 코드를 호스트에서 sanitizer 로 돌리고, 타깃에선 싼 검사만 남긴다"가 일반 전략이다.

</details>

<details>
<summary>Q6. uint32_t 말고 아무 타입이나 담는 fixed_vec 을 C 로 만들려면?</summary>

두 방법. ① `void *data` + `elem_size` 필드, `memcpy` 로 원소 복사 — 코드 한 벌, 대신 타입 검사 없음·간접 비용. ② 매크로로 타입별 구조체와 함수를 생성 (`FV_DECLARE(u32, uint32_t)` → `fv_u32_push` ...) — 타입 안전·인라인 가능, 대신 코드 크기 증가. 플래시가 작은 MCU 에서는 ①, 성능·안전이 중요하면 ②. C++ 이라면 `etl::vector<T, N>` (Embedded Template Library) 가 정확히 이 구조.

</details>

<details>
<summary>Q7. 용량을 "컴파일 타임에" 정하는 것의 장점은? _Static_assert 는 왜 넣었나요?</summary>

RAM 사용량이 `.bss` 에 고정되어 링크 단계에서 넘치면 바로 에러가 난다 (런타임에 처음 알게 되는 것보다 훨씬 싸다). `_Static_assert((CAP) > 0, ...)` 는 0 용량처럼 말이 안 되는 설정을 빌드에서 막는다. 실무에서는 "링 버퍼 크기는 2의 거듭제곱" (DS-B04) 같은 조건도 같은 방식으로 강제한다.

</details>

## 9. 추가 연습

### 연습 1 — swap-remove: O(1) 삭제

`fv_swap_remove(v, i, &out)` — 순서를 포기하는 대신 O(1). `[10,20,30,40,50]` 에서 1번을 지우면 `[10,50,30,40]`.

<details>
<summary>답 보기</summary>

`*out = d[i]; d[i] = d[--len];` — 마지막 원소를 구멍으로 옮긴다. `i` 가 마지막이면 자기 자신에게 복사하는 셈이라 무해. 게임 엔진의 엔티티 목록, 펌웨어의 "활성 타이머 목록"처럼 순서가 의미 없는 집합에 쓴다.

</details>

### 연습 2 — remove_if: 조건에 맞는 것 모두 지우기 (O(n))

`fv_remove_if(v, pred)` — 지운 개수를 반환, 남은 순서 유지. erase 를 반복 호출하면 O(n²) 이 된다.

<details>
<summary>답 보기</summary>

**읽기 포인터 r, 쓰기 포인터 w** 두 개. `r` 로 전부 훑으면서 살릴 원소만 `d[w++] = d[r]`. 끝나면 `len = w`. 각 원소를 최대 한 번 옮기므로 O(n). C++ `std::remove_if` 와 같은 알고리즘이고, NAND FTL 의 가비지 컬렉션에서 "유효 페이지만 새 블록으로 모으기"도 같은 모양이다.

</details>

### 연습 3 — 정렬 유지 삽입 (lower_bound + insert)

`fv_sorted_insert(v, x)` — 항상 오름차순을 유지. 중복 허용.

<details>
<summary>답 보기</summary>

이진 탐색으로 "x 이상인 첫 위치" `lower_bound` 를 찾고 `fv_insert(v, pos, x)`. 반열린 구간 `[lo, hi)` 에서 `mid = lo + (hi - lo) / 2`, `d[mid] < x` 면 `lo = mid + 1`, 아니면 `hi = mid`. 찾기 O(log n), 이동 O(n). 센서 보정 테이블(lookup + 선형 보간)이나 정렬된 bad block 목록에 쓴다.

</details>

### 연습 4 — append_n: 전부 아니면 전무

`fv_append_n(v, src, n)` — n개를 한꺼번에 붙인다. 다 안 들어가면 **하나도 넣지 않고** FULL.

<details>
<summary>답 보기</summary>

`if (n > cap - len) return FV_ERR_FULL;` — `len + n > cap` 은 `n = SIZE_MAX` 같은 값에서 덧셈이 넘쳐 통과해버린다 (테스트에 `(size_t)-1` 케이스가 있다). 통과하면 `memcpy(&d[len], src, n*4)` (여기는 다른 배열이라 겹치지 않음). 패킷 하나를 통째로 큐에 넣거나 버려야 하는 UART/USB 수신 처리와 같은 원칙: **부분 성공은 디버깅 지옥**.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 12/12 passed
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined practice.c -o pr_asan && ./pr_asan
```

## 10. 핵심 정리

- 펌웨어의 동적 배열 = **정적 배열 + len + cap**. 용량은 컴파일 타임 상수, `FV_DEFINE` 매크로로 저장소와 핸들을 같이 만든다.
- 유효 인덱스: get/set/erase 는 `i < len`, insert 만 `i <= len`. `cap` 은 FULL 검사에만.
- 인덱스는 `size_t` → `i >= len` 하나로 음수·초과 모두 거른다.
- **검사 먼저, 변경은 나중**. 실패한 호출은 상태를 바꾸지 않는다.
- 같은 배열 안 이동은 `memmove`. 개수: insert `len-i`, erase `len-i-1`. 비용 O(n), 평균 len/2.
- 순서 불필요 → swap-remove O(1). FIFO → 링 버퍼. 여러 개 삭제 → 두 포인터 압축 O(n).
- 용량 덧셈 검사는 뺄셈으로: `n > cap - len`.
- 검증: 경계 테스트 + 캐너리(타깃용, 구조체 내부 넘침) + ASan(호스트, 읽기/쓰기 즉시) + 고정 시드 참조 모델 비교.

## 참고자료

- [cppreference — memmove / memcpy (C)](https://en.cppreference.com/w/c/string/byte/memmove)
- [Clang AddressSanitizer 문서](https://clang.llvm.org/docs/AddressSanitizer.html)
- [Embedded Template Library — etl::vector (고정 용량 벡터)](https://www.etlcpp.com/vector.html)
- [FreeRTOS — Static vs Dynamic Memory Allocation](https://www.freertos.org/Static_Vs_Dynamic_Memory_Allocation.html)
- [Zephyr — K_MSGQ_DEFINE 등 정적 정의 매크로](https://docs.zephyrproject.org/latest/kernel/services/data_passing/message_queues.html)
- MISRA C:2012 Rule 21.3 — `<stdlib.h>` 의 메모리 할당 함수 사용 금지
- CLRS (Introduction to Algorithms) 17장 — 동적 테이블과 상각 분석 (std::vector 가 자라는 방식과 비교)
