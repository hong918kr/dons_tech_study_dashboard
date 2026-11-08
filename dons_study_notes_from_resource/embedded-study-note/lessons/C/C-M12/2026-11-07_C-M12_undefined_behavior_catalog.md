---
id: C-M12
time: 90분
updated: 2026-11-07
---
# C-M12 정의되지 않은 동작(UB) 카탈로그

## 학습 목표

- "컴파일러는 UB 가 **일어나지 않는다고 가정** 하고 최적화한다" 는 한 문장으로 `x + 1 > x` → `return 1`, NULL 검사 삭제, `<= 4` 루프 → `return true` 를 asm 과 함께 설명할 수 있다.
- 대표 UB 9가지(signed overflow, NULL 역참조 후 검사, 초기화 안 된 변수, 배열 범위 밖, 시프트, unsequenced 수정, dangling 포인터, strict aliasing, `INT_MIN / -1`)를 **-O0 / -O2 실측 결과** 와 C11 조항으로 구분한다.
- 각 UB 를 무엇이 잡는지 — `-Wall`, UBSan, ASan, `clang --analyze`, 그리고 **아무것도 못 잡는 것** — 를 표로 말할 수 있다.
- 넘침 검사 산술(add/sub/mul/div/shl), 감김(wrap) 없는 범위 검사, 신뢰할 수 없는 인덱스 검사를 **계산 전에 검사하는** 방식으로 UB 없이 구현한다.
- 펌웨어 명령 패킷 인터프리터를 "검사 → 임시 변수에 계산 → 한 번에 커밋" 구조로 작성해 sanitizer 아래에서 깨끗하게 통과시킨다.

## 1. 문제

UB 는 "-O0 디버그 빌드에선 되는데 -O2/-Os 릴리스 펌웨어에서만 깨지는" 버그의 1순위 원인이다. 면접에서는 "`x + 1 > x` 는 항상 참인가요?", "역참조 다음 줄의 NULL 검사가 왜 사라졌죠?", "`INT_MIN / -1` 은?" 처럼 짧은 코드로 나오고, 꼬리질문은 늘 "그럼 어떻게 안전하게 쓰죠?" 다.

C 표준의 세 가지 "정해지지 않음" (C-B14 에서 분류 연습을 했다):

| 종류 | 정의 (C11) | 예 | 컴파일러가 할 수 있는 일 |
|---|---|---|---|
| 구현 정의 (implementation-defined) | 3.4.1 — 구현이 고르고 **문서화** | `-1 >> 1`, `int` 크기 | 문서대로 일관되게 |
| 미지정 (unspecified) | 3.4.4 — 몇 가지 중 하나, 문서화 의무 없음 | `f() + g()` 호출 순서 | 허용된 것 중 아무거나 |
| **정의되지 않음 (undefined)** | 3.4.3 — 표준이 **아무 요구도 하지 않음** | `INT_MAX + 1`, `*NULL` | **무엇이든.** "이런 일은 없다" 고 가정해도 됨 |

핵심은 마지막 칸이다. 하드웨어가 무엇을 하느냐(arm64 의 `add` 는 그냥 감긴다)는 상관없다. **최적화기는 UB 를 "프로그램이 절대 밟지 않는 경로" 라는 전제(premise)로 쓴다.** 그 전제에서 논리적으로 따라 나오는 결론은 무엇이든 코드로 만든다.

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">int plus_one_gt(int x) { return x + 1 &gt; x; }  — 같은 소스, 두 개의 추론 (Apple clang 21, arm64 실측)</text>
  <rect x="10" y="40" width="350" height="190" rx="8" class="f-mut" stroke="currentColor"/>
  <rect x="380" y="40" width="350" height="190" rx="8" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="25" y="64" font-weight="bold">-O0 : 소스를 그대로 기계어로</text>
    <text x="25" y="90">① x 를 읽는다 (0x7FFFFFFF)</text>
    <text x="25" y="112">② add → 하드웨어가 감김 → 0x80000000</text>
    <text x="25" y="134">③ cmp → -2147483648 &gt; 2147483647 ? 아니오</text>
    <text x="25" y="156">④ 결과 0</text>
    <text x="25" y="200">"하드웨어가 하는 일" 을 보여줄 뿐 — 이것도</text>
    <text x="25" y="218">표준이 약속한 답은 아니다</text>
    <text x="395" y="64" font-weight="bold">-O2 : 표준이 허락한 추론</text>
    <text x="395" y="90">① signed 덧셈은 넘치지 않는다 (넘치면 UB)</text>
    <text x="395" y="112">② 그러면 x + 1 은 수학적으로 x 보다 크다</text>
    <text x="395" y="134">③ 비교는 항상 참 → x 를 볼 필요도 없다</text>
    <text x="395" y="200">INT_MAX 를 넣으면 1 — -O0 과 다른 답.</text>
    <text x="395" y="218">컴파일러 버그가 아니라 소스 버그</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="40" y="180">add w8,w8,#1 ; subs ; cset gt → 0</text>
    <text x="415" y="166">_plus_one_gt:</text>
    <text x="415" y="182">    mov  w0, #1</text>
  </g>
  <text x="10" y="256" fill="currentColor">UB 는 "이상한 값 하나" 로 끝나지 않는다. 앞뒤 코드를 지우는 "근거" 가 된다.</text>
  <text x="10" y="276" fill="currentColor">고치는 법은 하나: UB 가 될 연산을 하기 전에 검사한다. 사후 검사는 이미 늦다.</text>
</svg>
```

**관찰 과제 (먼저 예측하고, 그다음 돌린다).** `demo_ub_catalog.c` 에 아래 9개가 함수 하나씩 들어 있다. 각 줄의 `-O0` 결과와 `-O2` 결과를 종이에 예측해 적은 뒤 실행해 비교하라 (답과 asm 은 5. 풀이).

| # | UB | 데모 함수 (핵심 줄) | 예측할 것 |
|---|---|---|---|
| ① | signed overflow | `x + 1 > x`, `for (int i = start; i > 0; i++) n++;` | `INT_MAX`, `INT_MAX-3` 일 때 |
| ② | 역참조 후 NULL 검사 | `int v = *p; if (p == NULL) return -1;` | 검사가 살아남나 |
| ③ | 초기화 안 된 변수 | `int x; if (c) x = 5; return x;` | `c == 0` 일 때 |
| ④ | 배열 범위 밖 | `for (i = 0; i <= 4; i++) if (table[i] == v) return true;` | 없는 값 42, 5 를 찾으면 |
| ⑤ | 시프트 양 ≥ 폭 | `(1u << n) - 1u`, n = 32 | 런타임 n vs 상수 32 |
| ⑥ | unsequenced 수정 | `*p++ << 8 \| *p++` | `{0x12, 0x34}` |
| ⑦ | dangling 포인터 | 지역 변수 주소를 반환 | 바로 읽을 때, 다른 호출 뒤 |
| ⑧ | strict aliasing | `*a = 1; *b = 0.0f; return *a;` (a, b 같은 주소) | 1 ? 0 ? |
| ⑨ | `INT_MIN / -1`, `x / 0` | `return a / b;` | arm64 vs x86-64 |

관찰이 끝나면 같은 일들을 **UB 없이** 하는 코드를 쓴다.

### Part A — 검사하는 산술 (①⑤⑨ 의 수정)

`add_i32`, `sub_i32`, `mul_i32`, `div_i32`, `shl_i32` — 모두 `bool f(..., int32_t *out)`. 결과가 `int32_t` 에 들어가면 `*out` 에 쓰고 `true`, 아니면 **`*out` 을 건드리지 않고** `false`. 조건: 연산을 하기 **전에** 검사한다. `int64_t` 로 넓히기와 `__builtin_*_overflow` 는 금지 (면접관이 "가장 넓은 타입이면?" 이라고 물을 때의 답이 되도록). 테스트는 경계값 16개의 모든 쌍 + 고정 시드 xorshift32 퍼즈 20000쌍을 `int64_t` 기준값과 대조한다.

### Part B — 검사하는 메모리 접근 (②④⑦ 의 수정)

- `range_ok(off, len, cap)` — `[off, off+len)` 이 `[0, cap)` 안인가. `off + len` 이 `size_t` 에서 한 바퀴 돌아도 속지 않아야 한다.
- `slot_at(tab, n, idx)` — 신뢰할 수 없는 **signed** 인덱스(`int32_t`)를 검사해 원소 주소 또는 `NULL`.
- `table_find(t, n, v, &pos)` — ④ 의 `<=` 를 고친 탐색. 못 찾으면 `*pos` 그대로.
- `fmt_hex32(v, buf, cap)` — `"0xDEADBEEF"` 를 **호출자 버퍼** 에 쓴다 (⑦ "지역 배열 주소 반환" 의 정석 수정). 자리가 모자라면 아무것도 쓰지 않고 `NULL`.

### Part C — 명령 패킷 인터프리터 (전부 + ⑥⑧)

호스트(디버그 콘솔, BLE, CAN)에서 오는 6바이트 패킷 `[op][reg][val: big-endian int32]` 로 레지스터 8개짜리 가상 머신을 조작한다. op: `1 SET`, `2 ADD`, `3 MUL`, `4 SHL`, `5 DIV`. 입력은 **전부 신뢰할 수 없다** — 길이, 레지스터 번호, op, 값.

- `i32_from_be(p)` — 4바이트 → `int32_t`. 포인터 캐스트(⑧), `*p++` 두 번(⑥), `p[0] << 24` 의 int 넘침(C-B02), `uint32_t → int32_t` 범위 밖 변환(구현 정의) 없이.
- `exec_packet(regs, pkt, n)` — 검사 순서 **길이 → 레지스터 → op → 연산**, 에러는 `ST_ERR_LEN/REG/OP/OVF/DIV0`. 에러면 `regs` 는 **한 비트도** 바뀌지 않는다.

## 2. Problem (English)

> **Undefined behavior: reproduce, then eliminate.**
>
> First run `demo_ub_catalog.c` at `-O0` and `-O2`, with `-fsanitize=undefined` and `-fsanitize=address`, and explain each divergence (signed overflow, null check after dereference, uninitialized read, out-of-bounds read, oversized shift, unsequenced modification, dangling pointer, strict aliasing, `INT_MIN / -1`). Then implement, with **no undefined behavior for any input** and without wider types or overflow builtins:
>
> 1. `bool add_i32(int32_t a, int32_t b, int32_t *out)`, and likewise `sub_i32`, `mul_i32`, `div_i32`, and `bool shl_i32(int32_t x, unsigned n, int32_t *out)` (left shift as `x * 2^n`). On failure return `false` and leave `*out` untouched.
> 2. `bool range_ok(size_t off, size_t len, size_t cap)`; `int32_t *slot_at(int32_t *tab, size_t n, int32_t idx)`; `bool table_find(const int32_t *t, size_t n, int32_t v, size_t *pos)`; `char *fmt_hex32(uint32_t v, char *buf, size_t cap)` writing `"0x%08X"` into the caller's buffer.
> 3. `int32_t i32_from_be(const uint8_t *p)` and `status_t exec_packet(int32_t regs[8], const uint8_t *pkt, size_t n)` for 6-byte packets `[op][reg][be32 value]`, ops SET/ADD/MUL/SHL/DIV. Validate length, then register index, then opcode, then the arithmetic. A rejected packet must not modify any register.
>
> Examples: `add_i32(INT32_MAX, 1, &r) == false`; `mul_i32(46341, 46341, &r) == false`; `div_i32(INT32_MIN, -1, &r) == false`; `shl_i32(1, 31, &r) == false`; `range_ok(SIZE_MAX, 2, 16) == false`; packet `05 00 00 00 00 00` → `ST_ERR_DIV0`.
>
> The solution must pass `-std=c11 -Wall -Wextra -Werror -O2` and run clean under `-fsanitize=address,undefined`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # SDK 27.2 링크가 깨져 있을 때
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 이 쏟아지고 "16/46 passed" — TODO 를 채워 46/46
```

관찰용 데모 두 개 (일부러 UB 가 들어 있다 → **`-Werror` 없이**, `-w` 로 경고를 끄고 빌드):

```bash
cc -std=c11 -O0 -w demo_ub_catalog.c -o ub_O0 && ./ub_O0           # ② 를 뺀 전부
cc -std=c11 -O2 -w demo_ub_catalog.c -o ub_O2 && ./ub_O2
./ub_O2 2                                                            # ② NULL 역참조 (맥: SIGSEGV)
cc -std=c11 -O2 -S -w demo_ub_catalog.c -o -                         # 무엇이 지워졌나
cc -std=c11 -g -O1 -fsanitize=undefined -w demo_ub_catalog.c -o ub_ubsan && ./ub_ubsan
cc -std=c11 -g -O1 -fsanitize=address -fno-omit-frame-pointer -w demo_ub_catalog.c -o ub_asan
./ub_asan 4 ; ASAN_OPTIONS=detect_stack_use_after_return=1 ./ub_asan 7
clang --analyze -std=c11 demo_ub_catalog.c -o /dev/null              # 정적 분석기
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -O2 -S demo_ub_cm4.c -o -   # Cortex-M4 asm
```

## 4. 힌트

<details>
<summary>힌트 1 — 덧셈: 넘칠 식을 넘치지 않는 식으로 옮겨 적는다</summary>

`a + b > INT32_MAX` 는 계산하는 순간 이미 UB 다. 부등식의 양변에서 `b` 를 빼서 `a > INT32_MAX - b` 로 바꾸면, `b > 0` 일 때 `INT32_MAX - b` 는 절대 넘치지 않는다. `b < 0` 이면 반대쪽 `a < INT32_MIN - b`. 뺄셈은 `b` 의 부호만 뒤집어 같은 논리.

</details>

<details>
<summary>힌트 2 — 곱셈: 부호 조합 4가지, 나눗셈으로 한계를 구한다</summary>

`a > 0, b > 0` 이면 `a > INT32_MAX / b` 가 넘침 조건. 나머지 세 조합도 "결과의 부호가 어느 한계(MAX 또는 MIN)를 향하는가" 를 보고 같은 모양을 만든다. 한계를 구하는 나눗셈 자체가 UB 가 되면 안 된다 — `INT32_MIN / -1` 이 나오는 조합이 없는지 확인하라 (CERT INT32-C 의 표가 정답 모양이다). `a == 0` 이나 `b == 0` 이면 언제나 성공.

</details>

<details>
<summary>힌트 3 — 범위 검사: 더하지 말고 빼라</summary>

`off + len <= cap` 은 `size_t` 라 UB 는 아니지만 감긴다: `off = SIZE_MAX, len = 2` → `1 <= 16` 참. `off <= cap` 을 먼저 확인하면 `cap - off` 는 음수가 될 수 없으니 `len <= cap - off` 로 비교한다. 포인터로 `p + len < p` 를 검사하는 것은 더 나쁘다 — 범위 밖 포인터 산술 자체가 UB 라 컴파일러가 그 검사를 지울 수 있다.

</details>

<details>
<summary>힌트 4 — Part C: 검사가 다 끝날 때까지 regs 를 만지지 않는다</summary>

결과는 지역 변수 `r` 에 계산하고, 실패하면 바로 반환, 성공한 경우에만 마지막 줄에서 `regs[reg] = r;`. Part A 함수들의 "실패하면 `*out` 을 건드리지 않음" 계약이 여기서 쓸모가 있다. `uint32_t` 를 `int32_t` 로 바꿀 때 `0x80000000` 이상은 `(int32_t)u` 가 구현 정의(6.3.1.3p3)이므로, `u - 0x80000000u` 를 먼저 범위 안으로 넣은 뒤 `INT32_MIN` 을 더한다.

</details>

## 5. 풀이

### 카탈로그 실측 — 한눈에

`demo_ub_catalog.c` 를 `-O0` 과 `-O2` 로 빌드해 실행한 **실제 출력** (Apple clang 21.0.0, arm64, macOS):

```text
-- O0                                          -- O2
[1] plus_one_gt(INT_MAX)    = 0                [1] plus_one_gt(INT_MAX)    = 1
[1] count_up(INT_MAX-3)     = 4                [1] count_up(INT_MAX-3)     = 0
[3] uninit(0)               = 79874428         [3] uninit(0)               = 5
[4] in_table(42)            = 0                [4] in_table(42)            = 1
[4] in_table(5)             = 0                [4] in_table(5)             = 1
[5] low_mask_rt(32)         = 0x00000000       [5] low_mask_rt(32)         = 0x00000000
[5] low_mask_32()           = 0x00000000       [5] low_mask_32()           = 0x00000029
[6] be16_oneliner({12,34})  = 0x1234           [6] be16_oneliner({12,34})  = 0x1234
[7] *make_local(): first=42 after_call=1       [7] *make_local(): first=1 after_call=1
[8] alias(&u, (float*)&u)   = 0                [8] alias(&u, (float*)&u)   = 1
[9] INT_MIN / -1            = -2147483648      [9] INT_MIN / -1            = -2147483648
[9] 7 / 0                   = 0                [9] 7 / 0                   = 0
```

9개 중 **6개(①③④⑤⑦⑧)가 최적화 수준에 따라 답이 바뀌었다.** 나머지 셋(②⑥⑨)은 이 맥에서 갈리지 않았는데, 그 이유도 각각 다르다 (아래). `-O2` 의 해당 함수들 asm (실제 출력, `.cfi` 줄 생략):

```text
_plus_one_gt:  mov w0, #1 ; ret                ← ① 비교 자체가 사라짐
_count_up:     mov w0, #0 ; ret                ← ① 루프가 사라짐
_read_then_check: ldr w0, [x0] ; ret           ← ② NULL 검사가 사라짐
_uninit:       mov w0, #5 ; ret                ← ③ 조건 c 를 보지도 않음
_in_table:     mov w0, #1 ; ret                ← ④ 테이블을 읽지도 않음
_low_mask_32:  ret                             ← ⑤ 아무것도 계산하지 않음 (w0 에 남은 값이 반환값)
_be16_oneliner: ldrh w8, [x0] ; rev16 w0, w8 ; ret
_make_local:   sub sp,sp,#16 ; add x0,sp,#12 ; add sp,sp,#16 ; ret   ← ⑦ 42 저장이 없음
_alias:        mov w8,#1 ; str w8,[x0] ; str wzr,[x1] ; mov w0,#1 ; ret  ← ⑧ *a 를 다시 안 읽음
_divi:         sdiv w0, w0, w1 ; ret
```

#### ① signed overflow — 루프가 통째로 사라진다

C11 6.5p5: 식의 결과가 그 타입으로 표현할 수 없으면 UB. (unsigned 는 6.2.5p9 로 모듈로 2^N 이 **정의** 되어 있다 — C-B02.) `count_up(INT_MAX-3)` 은 `-O0` 에서 4번 돌고 `i` 가 음수로 감겨서 끝난다. `-O2` 는 0 이다. 두 개의 규칙이 겹친 결과인데, 플래그로 하나씩 꺼 보면 추론 사슬이 그대로 보인다 (실측):

| 빌드 | `_count_up` asm | 의미 |
|---|---|---|
| `-O2` | `mov w0,#0 ; ret` | 아래 두 단계를 다 적용 |
| `-O2 -fno-finite-loops` | `cmp w0,#1 ; b.lt → mov w0,#0` / 아니면 `LBB1_1: b LBB1_1` | 1단계만: "i++ 는 안 넘친다 → `start > 0` 이면 영원히 `i > 0` → 무한 루프" |
| `-O2 -fwrapv` | `sub w8, 0x80000000, w0 ; csel ... gt` | 감김을 정의된 동작으로 → 정확한 닫힌 식 `0x80000000 - start` = 4 |

2단계는 C11 6.8.5p6: 제어식이 상수가 아니고 본문에 I/O·volatile·원자 연산이 없는 반복문은 **끝난다고 가정해도 된다** (C-B09 의 `while(!ready){}` 와 같은 조항). "무한 루프인데 끝나야 한다" 는 모순 → `start > 0` 경로는 일어날 수 없다 → `n = 0`. Cortex-M4 로 크로스 컴파일해도 똑같이 `movs r0, #0 ; bx lr` (demo_ub_cm4.c).

UBSan 실측: `demo_ub_catalog.c:24:41: runtime error: signed integer overflow: 2147483647 + 1 cannot be represented in type 'int'` (27행 루프도 같은 보고). `-Wall -Wextra` 는 아무 말도 하지 않는다. **수정**: 비교를 넘치지 않게 `x < INT_MAX`, 루프는 상한을 명시하거나 unsigned 카운터. 계산이 필요하면 Part A 의 사전 검사.

> 왜 컴파일러가 이런 "권리" 를 갖나: `for (int i = 0; i < n; i++) a[i]...` 에서 `i` 가 넘치지 않는다는 가정 덕분에 64비트 머신에서 `i` 를 64비트 레지스터로 넓혀 주소 계산을 단순화하고, 반복 횟수를 미리 계산해 벡터화한다. Linux 커널처럼 이 가정을 버리는 프로젝트는 `-fno-strict-overflow`(≈ `-fwrapv`) 로 빌드한다.

#### ② 역참조한 뒤의 NULL 검사 — 맥에선 안 보이고 Cortex-M 에선 조용한 오답

C11 6.5.3.2p4: 무효한 포인터(NULL 포함, 각주 102)에 단항 `*` 를 쓰면 UB. `int v = *p;` 가 실행됐다면 "p 는 NULL 이 아니었다" 가 전제가 되고, 다음 줄의 `if (p == NULL)` 은 죽은 코드다. `-O0` asm 에는 `ldr w8,[x8]` 다음에 `cbnz x8` 검사가 남아 있고, `-O2` 는 `ldr w0,[x0] ; ret` 뿐이다.

그런데 **맥에서는 결과가 갈리지 않는다** — `./ub_O0 2`, `./ub_O2 2` 둘 다 rc=139 (SIGSEGV). macOS arm64 프로세스는 낮은 4GB 를 `__PAGEZERO` 로 막아 두어서, 검사에 도달하기 전에 읽기가 죽는다. 차이를 보려면 검사를 지우지 말라고 해 보면 된다:

```text
cc -O2 -fno-delete-null-pointer-checks:   _read_then_check: cbz x0, LBB2_2 ; ldr w0,[x0] ; ret ; LBB2_2: mov w0,#-1 ; ret
./f 2  →  [2] read_then_check(NULL) = ...  -1      (rc=0)
```

검사를 앞으로 당기고 읽기를 뒤로 보냈다 — 원래 소스의 NULL 경로는 UB 이므로 이렇게 바꾸는 것도 합법이다. Cortex-M 에서는 이야기가 무섭다. 주소 0 은 보통 **벡터 테이블(초기 MSP 값)** 이 있는 읽을 수 있는 플래시라 `*NULL` 이 폴트 없이 `0x2002_0000` 같은 값을 돌려준다. 그리고 검사가 지워졌으니 그 값을 정상 온도처럼 쓴다. `demo_ub_cm4.c` 실측:

```text
read_then_check:  ldr r0, [r0] ; bx lr                          ← 기본: 검사 삭제
  (-fno-delete-null-pointer-checks)  cmp r0,#0 ; ite ne ; ldrne r0,[r0] ; moveq.w r0,#-1 ; bx lr
sensor_temp:      cmp r0,#0 ; itee ne ; ldrne r0,[r0,#4] ; movweq/movteq r0,#-999 ; bx lr
```

마지막 줄이 흥미롭다: 같은 실수를 구조체 멤버(`s->temp`)로 쓰면 이 clang 은 검사를 **남겼다**. UB 의 결과는 같은 컴파일러 안에서도 코드 모양에 따라 달라진다는 증거다. 2009년 Linux `tun` 드라이버(CVE-2009-1897)에서는 GCC 가 바로 이 모양(`sk = tun->sk; if (!tun) ...`)의 검사를 지워 권한 상승 취약점이 됐고, 그 뒤로 커널은 `-fno-delete-null-pointer-checks` 로 빌드한다. UBSan 은 `runtime error: load of null pointer of type 'const int'` 를 찍고, `clang --analyze` 는 `Dereference of null pointer [core.NullDereference]` 로 잡는다. **수정**: 검사가 먼저, 역참조는 나중 (Part B `slot_at`).

#### ③ 초기화 안 된 지역 변수 — 디버거를 붙이면 사라지는 버그

`int x;` 의 값은 불확정(indeterminate, 6.7.9p10)이고, 주소를 한 번도 취하지 않은 자동 변수를 초기화 전에 읽으면 UB 다 (6.3.2.1p2). `-O0` 은 스택 쓰레기를 돌려준다 — 세 번 실행하니 `49858804`, `80840948`, `6179060` (ASLR 로 매번 다름). `-O2` 는 `mov w0,#5` 다: LLVM 에게 `c == 0` 경로의 `x` 는 `undef` 라 "아무 값이나 골라도 되고", 5 를 고르면 분기 자체가 필요 없다.

| 도구 | 결과 (실측) |
|---|---|
| `-Wall` | `warning: variable 'x' is used uninitialized whenever 'if' condition is false [-Wsometimes-uninitialized]` (`-Wall` 없으면 조용) |
| UBSan / ASan | 보고 없음 — 이 둘은 초기화 여부를 추적하지 않는다 |
| MSan | `clang: error: unsupported option '-fsanitize=memory' for target 'arm64-apple-darwin27.2.0'` — 리눅스 전용 |
| `clang --analyze` | `Undefined or garbage value returned to caller [core.uninitialized.UndefReturn]` |
| `-ftrivial-auto-var-init=zero` / `=pattern` | `uninit(0)` = `0` / `-1431655766` (0xAAAAAAAA) — 결정적이 되어 재현은 쉬워진다 |

펌웨어에서는 스택 쓰레기가 "직전에 어떤 함수가 불렸나" 에 달려 있어서, 브레이크포인트를 걸거나 로그 한 줄을 넣으면 값이 바뀌어 버그가 숨는다. **수정**: 선언과 동시에 초기화, 또는 모든 경로에서 대입 (Part C 의 `r` 은 성공 경로에서만 읽히도록 구조를 짰다).

#### ④ 배열 범위 밖 — 없는 값을 "있다" 고 답한다

6.5.6p8: 배열 끝(one-past) 너머를 가리키는 포인터 산술과, one-past 위치의 역참조는 UB. `in_table` 은 i = 0..4 를 돈다. 컴파일러의 추론: "i = 4 에 도달해 `table[4]` 를 읽으면 UB → 그런 실행은 없다 → 반드시 i = 0..3 중 하나에서 `return true` 했다". 결과는 `mov w0,#1 ; ret` — 테이블을 **읽지도 않고** 42 도 5 도 "있다". John Regehr 가 GCC 로 보여준 고전 예제인데, clang 21 도 `-O2` 에서 그대로 재현됐다 (`-O1` 은 아직 0, Cortex-M4 도 `movs r0, #1`). `-O0` 은 `table[4]` 자리에 우연히 있던 0 을 읽고 0 을 답했다.

`-Wall -Wextra` 는 **경고하지 않는다** (인덱스가 상수가 아니면 `-Warray-bounds` 가 못 본다). 런타임 도구는 둘 다 잡는다:

```text
UBSan: demo_ub_catalog.c:49:13: runtime error: index 4 out of bounds for type 'int[4]'
ASan : ERROR: AddressSanitizer: global-buffer-overflow on address 0x000104ce51f0 ...
       READ of size 4 ... #0 in in_table demo_ub_catalog.c:49
       0x000104ce51f0 is located 0 bytes after global variable 'table' ... of size 16
```

**수정**: `i < 4` 보다 `i < n` (크기를 함께 넘기고 `sizeof t / sizeof t[0]` 로 계산) — Part B `table_find`.

#### ⑤ 시프트 양 ≥ 폭 — 함수가 `ret` 한 줄이 된다

6.5.7p3: 시프트 양이 음수이거나 승격된 왼쪽 피연산자의 폭 이상이면 UB (상세 표는 C-B03). 런타임 `n = 32` 는 두 수준 모두 0 — arm64 `lsl w` 가 양을 32로 나눈 나머지(0)만 쓰기 때문에 `~(-1 << 0) = 0` 이다 (`-O2`: `mov w8,#-1 ; lsl w8,w8,w0 ; mvn w0,w8`). 하지만 **상수 32 가 인라인되면** LLVM 은 결과를 poison 으로 보고 계산을 아예 생략한다: `_low_mask_32: ret`. 반환값 `0x29` = 41 은 직전 `printf` 가 돌려준 **출력 글자 수**(`"[5] low_mask_rt(32)         = 0x00000000\n"` 은 41자)가 `w0` 에 남아 있던 것이다. UBSan: `runtime error: shift exponent 32 is too large for 32-bit type 'unsigned int'`, `--analyze`: `Left shift by '32' overflows the capacity of 'unsigned int'`. **수정**: `n >= 32 ? 0xFFFFFFFFu : (1u << n) - 1u` (C-B01 `field_mask`), signed 왼쪽 시프트는 Part A `shl_i32`.

#### ⑥ unsequenced 수정 — clang 에선 갈리지 않았다 (그래도 UB)

6.5p2: 같은 스칼라 객체(`p`)에 대한 두 부작용이 서로 순서가 없으면 UB (C-B14 Q7/Q9). `be16_oneliner` 는 `-O0`, `-O2` 모두 `0x1234` — clang 은 왼쪽부터 평가했고, `-O2` 는 아예 `ldrh + rev16` (16비트 로드 후 바이트 뒤집기)로 합쳤다. **이 맥에선 갈리지 않는다는 것이 정직한 결과다.** 하지만 표준이 순서를 주지 않으니 다른 컴파일러(GCC 는 이런 식에서 clang 과 다른 순서를 고르는 경우가 알려져 있다 — 여기엔 GCC 가 없어 확인 못 함)나 다른 버전에서 `0x3412` 가 나와도 할 말이 없다. 잡는 도구는 컴파일 경고뿐: `warning: multiple unsequenced modifications to 'p' [-Wunsequenced]` — **`-Wall` 없이도 기본으로 켜져 있다.** UBSan/ASan/`--analyze` 는 침묵. **수정**: `(uint16_t)((unsigned)p[0] << 8 | p[1])` — 인덱스로 읽으면 부작용이 없다 (Part C `i32_from_be`).

#### ⑦ dangling 포인터 — 42 를 저장하는 코드가 없다

6.2.4p2: 수명이 끝난 객체를 참조하면 UB 이고, 그 객체를 가리키던 포인터 값은 불확정이 된다. `make_local()` 이 돌려준 주소를 읽으면 `-O0` 에선 처음엔 42 (스택 칸이 아직 안 덮임), 다른 함수를 부른 뒤엔 `1`. `-O2` 는 처음부터 `1` — asm 을 보면 `make_local` 안에 **42 를 저장하는 명령이 없다**. 함수가 끝나면 죽는 객체에 쓰는 것은 관찰 불가능한 죽은 저장(dead store)이라 지웠다. (`return &local;` 을 직접 쓰면 `-Wall` 없이도 `-Wreturn-stack-address` 경고가 나고 clang 은 그래도 스택 주소를 돌려준다. GCC 는 이 경우 NULL 을 돌려주는 것으로 알려져 있다 — 미확인.)

```text
ASan (기본 옵션)  : [7] *make_local(): first=42 after_call=42   ← 보고 없음!
ASAN_OPTIONS=detect_stack_use_after_return=1 :
  ERROR: AddressSanitizer: stack-use-after-return on address 0x0001065e1020 ...
  READ of size 4 ... #0 in main demo_ub_catalog.c:112
  [32, 36) 'local' (line 63) <== Memory access at offset 32 is inside this variable
clang --analyze   : Address of stack memory associated with local variable 'local' returned to caller
```

펌웨어 판: 지역 배열을 **DMA 버퍼** 로 넘기고 함수가 리턴하는 버그 — 전송이 끝나기 전에 그 스택 칸을 다른 함수가 쓰거나, 수신 DMA 가 남의 스택 프레임(복귀 주소 포함)을 덮는다. RTOS 큐에 지역 변수 주소를 넣는 것도 같다. **수정**: 호출자가 버퍼를 넘긴다 (Part B `fmt_hex32`), 값으로 반환, 또는 `static` (재진입 불가에 주의).

#### ⑧ strict aliasing — 간단히 (깊이는 C-M13)

6.5p7: 객체는 자기 타입(또는 호환·부호 변형·문자 타입 등)의 lvalue 로만 접근해야 한다. `int *a` 와 `float *b` 는 같은 객체를 가리킬 수 없다고 가정 → `*b = 0.0f` 뒤에도 `*a` 는 1 이라고 `mov w0,#1`. `-O0` 은 0, `-O2` 는 1, `-O2 -fno-strict-aliasing` 은 다시 0 (실측). UBSan·ASan 모두 침묵. 전용 도구 TypeSanitizer(`-fsanitize=type`)는 이 Apple clang 에서 `libclang_rt.tysan_osx_dynamic.dylib` 가 없어 **링크 에러** 였다. **수정**: `memcpy` 또는 바이트 단위 조립 (Part C, 그리고 C-M13 에서 asm 비교까지).

#### ⑨ 보너스: `INT_MIN / -1` 과 `x / 0` — CPU 마다 다르다

6.5.5p5: 오른쪽 피연산자가 0 이면 UB. 6.5.5p6: `a / b` 의 몫을 표현할 수 없으면 `a / b` 와 **`a % b` 둘 다** UB. 수학적 몫 2^31 은 `int` 에 없다. 결과는 CPU 가 정한다 (실측):

| 실행 환경 | `INT_MIN / -1` | `7 / 0` |
|---|---|---|
| arm64 (`sdiv`) `-O0`/`-O2` | `-2147483648` (조용히 감김) | `0` (조용히) |
| x86-64 (`-arch x86_64`, Rosetta 2) | **SIGFPE, rc=136** (`idiv` 가 #DE 예외) | (도달 전 종료) |
| Cortex-M3/M4 `SDIV` (아키텍처 매뉴얼 기준) | `0x80000000` | `0`, 단 `SCB->CCR.DIV_0_TRP = 1` 이면 UsageFault |

UBSan: `runtime error: division of -2147483648 by -1 cannot be represented in type 'int'` (같은 소스 위치의 두 번째 보고는 UBSan 이 중복 제거해서 `/0` 은 따로 돌렸다: `runtime error: division by zero`). **수정**: Part A `div_i32`, 추가 연습 4 `rem_i32`.

### Part A — "계산 전에 검사" 를 식으로 만들기

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">add_i32: a + b 가 넘치는가?  → a 가 어느 구간에 있는지로 바꿔 묻는다</text>
  <line x1="40" y1="90" x2="700" y2="90" stroke="currentColor"/>
  <line x1="40" y1="80" x2="40" y2="100" stroke="currentColor"/>
  <line x1="700" y1="80" x2="700" y2="100" stroke="currentColor"/>
  <line x1="370" y1="82" x2="370" y2="98" stroke="currentColor"/>
  <text x="20" y="118" fill="currentColor">INT32_MIN</text>
  <text x="660" y="118" fill="currentColor">INT32_MAX</text>
  <text x="365" y="118" fill="currentColor">0</text>
  <rect x="560" y="70" width="140" height="16" class="f-warn" stroke="currentColor"/>
  <line x1="560" y1="60" x2="560" y2="100" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="500" y="56" fill="currentColor">INT32_MAX − b</text>
  <text x="575" y="66" fill="currentColor" font-size="12">여기의 a 는 넘친다</text>
  <rect x="40" y="94" width="120" height="16" class="f-warn" stroke="currentColor"/>
  <line x1="160" y1="84" x2="160" y2="125" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="120" y="140" fill="currentColor">INT32_MIN − b  (b &lt; 0 일 때)</text>
  <text x="10" y="170" fill="currentColor">b &gt; 0 : a + b &gt; MAX  ⇔  a &gt; MAX − b       (MAX − b 는 0..MAX−1 — 넘칠 수 없다)</text>
  <text x="10" y="192" fill="currentColor">b &lt; 0 : a + b &lt; MIN  ⇔  a &lt; MIN − b       (MIN − b 는 MIN+1..−1 — 넘칠 수 없다)</text>
  <text x="10" y="214" fill="currentColor">b = 0 : 절대 안 넘친다.   뺄셈은 b 의 부호를 뒤집어: b &lt; 0 이면 a &gt; MAX + b, b &gt; 0 이면 a &lt; MIN + b</text>
  <text x="10" y="238" fill="currentColor">흔한 오답 if (a + b &lt; a) — 이미 UB 가 일어난 값을 검사한다. ① 처럼 -O2 가 이 검사를 지울 수 있다.</text>
</svg>
```

**곱셈** 은 결과 부호가 어느 한계를 향하는지로 나눈다 (CERT INT32-C 와 같은 모양):

| a | b | 결과 향하는 쪽 | 넘침 조건 | 한계 계산이 안전한 이유 |
|---|---|---|---|---|
| > 0 | > 0 | MAX | `a > MAX / b` | 양수 ÷ 양수 |
| > 0 | ≤ 0 | MIN | `b < MIN / a` | a > 0 이라 `MIN / -1` 불가 |
| ≤ 0 | > 0 | MIN | `a < MIN / b` | b > 0 |
| ≤ 0 | ≤ 0 | MAX | `a != 0 && b < MAX / a` | a < 0 이고 `MAX / a` 는 언제나 표현 가능 |

`INT32_MIN × 1` 은 성공, `INT32_MIN × -1` 과 `46341 × 46341`(= 2,147,488,281)은 실패, `46340²` = 2,147,395,600 은 성공 — 이 네 개가 경계 테스트의 핵심이다. **나눗셈** 은 실패 입력이 딱 둘: `b == 0`, `a == INT32_MIN && b == -1`. **signed 시프트** 는 6.5.7p4: `x` 가 음수가 아니고 `x × 2^n` 이 표현 가능할 때만 정의 → `x < 0 || n >= 32` 거절, 그다음 `x > (INT32_MAX >> n)` 이면 넘침. `n = 31` 이면 `INT32_MAX >> 31 = 0` 이라 `x = 0` 만 통과한다 (`1 << 31` 은 UB — C-B03).

비용은? `demo_ub_cm4.c` 에 같은 코드를 넣어 Cortex-M4 로 컴파일한 실측: `add_i32` 는 비교·분기 몇 개(나눗셈 없음), `mul_i32` 는 경로마다 `udiv` 1개(M4 에서 2~12 사이클). 32비트 값이라면 넓혀서 검사하는 쪽이 더 싸다 — `mul_i32_wide` 는 `smmulr` + `smulleq` 로 끝나고 나눗셈이 없다. 나눗셈 방식은 "이미 가장 넓은 타입(`int64_t`, `size_t`)" 일 때의 답이다. 실무에선 GCC/clang 의 `__builtin_add_overflow` 가 arm64 `adds ; cset vc`, Cortex-M `adds ; it vc` 로 가장 짧다 (실측) — 단 비표준이고, `*out` 에 감긴 값을 항상 써 버린다는 점이 이 문제의 계약과 다르다. C23 은 이를 `<stdckdint.h>` 의 `ckd_add/ckd_sub/ckd_mul` 로 표준화했다.

### Part B — 메모리 접근은 "주소를 만들기 전에" 검사

- `range_ok`: `off <= cap && len <= cap - off`. 두 번째 비교는 첫 번째가 참일 때만 평가되므로(`&&` 단락 평가) `cap - off` 가 감기지 않는다. `off + len <= cap` 은 `off = SIZE_MAX, len = 2` 에서 `1 <= 16` 으로 통과해 버린다 — UB 는 아니지만(unsigned) 보안 버그다. 포인터 버전 `if (p + len > end)` 은 더 나쁘다: `p + len` 이 배열 밖이면 그 산술 자체가 UB(6.5.6p8)라 컴파일러가 검사를 `len > end - p` 로 바꾸거나 지울 수 있다.
- `slot_at`: 신뢰할 수 없는 signed 인덱스는 **음수부터** 거른다. `(size_t)idx >= n` 만 쓰면 `-1` 이 `SIZE_MAX` 가 되어 우연히 걸러지긴 하지만, 의도를 코드로 남기는 쪽이 리뷰·MISRA 에 맞다. 그리고 NULL 검사는 역참조보다 **먼저** (②).
- `table_find`: `i < n`. 크기를 함수 인자로 받으면 호출자가 `sizeof t / sizeof t[0]` 로 넘긴다 — 배열이 포인터로 decay 된 뒤에는 크기를 알 방법이 없다 (C-B06).
- `fmt_hex32`: 결과의 수명을 **호출자** 가 정하게 한다. `snprintf` 와 같은 모양(버퍼 + 크기)이고, 자리가 모자라면 아무것도 쓰지 않는다. `static char buf[11]` 로 돌려주는 방법도 UB 는 아니지만 ISR 과 메인이 동시에 부르면 서로 덮어쓴다 (재진입 불가).

### Part C — 검사, 임시 계산, 커밋

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">exec_packet: 신뢰할 수 없는 6바이트 → 검사 사슬 → 성공할 때만 regs 에 한 번 쓴다</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="60" height="30" class="f-acc"/>
    <rect x="70" y="40" width="60" height="30" class="f-acc"/>
    <rect x="130" y="40" width="240" height="30" class="f-mut"/>
  </g>
  <g fill="currentColor">
    <text x="28" y="60">op</text><text x="85" y="60">reg</text>
    <text x="150" y="60">val: big-endian int32 (4바이트)</text>
    <text x="390" y="60" font-size="12">i32_from_be: 바이트로 조립, 캐스트 없음 (⑥⑧ 회피)</text>
  </g>
  <g stroke="currentColor">
    <rect x="10" y="100" width="100" height="40" rx="6" class="f-mut"/>
    <rect x="130" y="100" width="100" height="40" rx="6" class="f-mut"/>
    <rect x="250" y="100" width="100" height="40" rx="6" class="f-mut"/>
    <rect x="370" y="100" width="150" height="40" rx="6" class="f-mut"/>
    <rect x="540" y="100" width="190" height="40" rx="6" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="22" y="125">n == 6 ?</text>
    <text x="142" y="125">reg &lt; 8 ?</text>
    <text x="262" y="125">op 1..5 ?</text>
    <text x="382" y="118">r = 검사 연산</text>
    <text x="382" y="133" font-size="12">add/mul/shl/div_i32</text>
    <text x="552" y="125">regs[reg] = r;  ST_OK</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M110 120 L130 120"/><path d="M230 120 L250 120"/><path d="M350 120 L370 120"/><path d="M520 120 L540 120"/>
    <path d="M60 140 L60 170"/><path d="M180 140 L180 170"/><path d="M300 140 L300 170"/><path d="M445 140 L445 170"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="186">ERR_LEN</text><text x="145" y="186">ERR_REG (④)</text>
    <text x="270" y="186">ERR_OP</text><text x="395" y="186">ERR_OVF / ERR_DIV0 (①⑤⑨)</text>
  </g>
  <rect x="10" y="200" width="510" height="26" class="f-warn" stroke="currentColor"/>
  <text x="20" y="218" fill="currentColor">어느 에러든 regs 는 그대로 — 실패 경로는 쓰기 전에 return</text>
  <text x="10" y="244" fill="currentColor">C-M15 의 goto cleanup 과 같은 정신: 부분적으로 바뀐 상태를 남기지 않는다.</text>
</svg>
```

`i32_from_be` 의 마지막 함정: `uint32_t u = 0xFFFFFFFE` 를 `(int32_t)u` 로 바꾸는 것은 **UB 는 아니지만 구현 정의** 다 (6.3.1.3p3, 신호를 낼 수도 있다고 적혀 있다). 모든 주요 컴파일러가 -2 를 주고 C23 이 2의 보수를 강제했지만, 이식성 있게 쓰려면 `u >= 0x80000000` 일 때 `(int32_t)(u - 0x80000000u) + INT32_MIN` — 괄호 안은 0..INT32_MAX 라 변환이 정확하고, 더하기 결과는 INT32_MIN..-1 이라 넘치지 않는다. 이렇게 써도 비용은 0 이다: clang `-O2` 는 이 함수 전체를 arm64 `ldr w8,[x0] ; rev w0,w8`, Cortex-M4 `ldr r0,[r0] ; rev r0,r0` 두 명령으로 만든다 (실측) — 두 분기가 같은 비트를 내니 분기가 사라지고, 바이트 조립은 `rev`(바이트 순서 뒤집기) 하나가 된다. 소스에선 바이트로 읽었으니 정렬·aliasing 문제는 컴파일러가 책임진다 (M4 의 `ldr` 은 비정렬도 허용, Cortex-M0 은 비정렬 접근이 안 되므로 `ldrb` 4개 + 시프트로 남는다, 실측 — C-M04).

검사 순서는 테스트가 고정한다: 레지스터 번호 9 + op 6 인 패킷은 `ST_ERR_REG`. SHL 의 `val` 은 `int32_t` 라 음수가 올 수 있으므로 `val >= 0 &&` 으로 먼저 거른 뒤에야 `(unsigned)val` 로 바꾼다 (`-1` 이 `4294967295` 가 되어도 `shl_i32` 가 거절하긴 하지만, 의도가 드러나게). DIV 는 0 을 `ST_ERR_DIV0` 로 먼저 분리하고 나머지 실패(`INT32_MIN / -1`)는 `ST_ERR_OVF`.

### 무엇이 무엇을 잡나 (이 맥에서 실측)

| # | UB | `-Wall -Wextra` | UBSan | ASan | `clang --analyze` |
|---|---|---|---|---|---|
| ① | signed overflow | – | ✔ | – | – |
| ② | NULL 역참조 | – | ✔ (null load) | (맥은 SEGV 로 어차피 죽음) | ✔ |
| ③ | 미초기화 읽기 | ✔ `-Wsometimes-uninitialized` | – | – | ✔ |
| ④ | 배열 범위 밖 | – | ✔ (`bounds`) | ✔ global-buffer-overflow | – |
| ⑤ | 시프트 폭 초과 | – (런타임 값) | ✔ | – | ✔ |
| ⑥ | unsequenced | ✔ `-Wunsequenced` (기본 켜짐) | – | – | – |
| ⑦ | dangling | – (직접 `return &local` 이면 ✔) | – | ✔ `detect_stack_use_after_return=1` 일 때만 | ✔ |
| ⑧ | strict aliasing | – | – | – | – |
| ⑨ | `INT_MIN/-1`, `/0` | – | ✔ | – | ✔ (`/0` 만) |

**어떤 도구도 9개를 다 잡지 못한다.** 그래서 펌웨어 팀의 현실적인 그물은: `-Wall -Wextra -Werror` + 순수 로직을 호스트로 빼서 UBSan/ASan 유닛 테스트 + 정적 분석(`--analyze`, MISRA 검사기) + 타깃에서 `-fsanitize=...  -fsanitize-trap` (런타임 없이 `udf` 트랩, C-B03 Q6) + 코드 리뷰 체크리스트.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 46/46 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 46/46, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `if (a + b < a) overflow();` | 넘침이 이미 일어난 뒤의 검사 — signed 면 UB 라 `-O2` 가 지울 수 있다 (①) | `if (b > 0 && a > INT32_MAX - b)` |
| `(int64_t)(a * b)` 로 "넓혀서 검사" | 곱이 `int` 에서 먼저 계산·넘침 — 캐스트가 늦었다 (C-B02) | `(int64_t)a * b` |
| `if (b != 0) q = a / b;` | `INT32_MIN / -1` 을 잊음 — x86 은 SIGFPE, 표준은 UB | `b == 0 \|\| (a == INT32_MIN && b == -1)` 거절 |
| `v = p->x; if (!p) return;` | 역참조가 "p 는 NULL 아님" 을 증명 → 검사 삭제 (②) | 검사를 먼저 |
| `for (i = 0; i <= N; i++) a[i]` | 마지막 반복이 UB → 루프 전체가 재해석될 수 있다 (④ `return true`) | `i < N` |
| `if (off + len > cap)` | `size_t` 가 감겨 거대한 `len`/`off` 가 통과 | `off > cap \|\| len > cap - off` |
| `char *f(void) { char buf[16]; ...; return buf; }` | 리턴 후 `buf` 는 죽은 객체 (⑦), `-O2` 는 쓰기 자체를 지울 수 있다 | 호출자 버퍼 + 크기 |
| `uint32_t v = *(uint32_t *)&pkt[2];` | strict aliasing(⑧) + 비정렬(C-M04) — Cortex-M0 는 HardFault | 바이트 조립 또는 `memcpy` |
| `int x; switch (op) { case 1: x = ...; } use(x);` | default 경로에서 미초기화 (③), `-Wall` 이 없으면 조용 | 모든 경로에서 대입 또는 초기화 |
| "`-O0` 에서 테스트 통과했으니 OK" | UB 는 최적화 수준·컴파일러 버전에 따라 결과가 바뀐다 (표: 6개가 갈림) | 릴리스와 같은 `-O2/-Os` 로 + sanitizer 로 테스트 |

## 8. 면접 Q&A

<details>
<summary>Q1. UB, 구현 정의, 미지정의 차이를 예와 함께 말해보세요.</summary>

구현 정의: 구현이 고르고 문서화해야 한다 — `int` 크기, signed 음수의 오른쪽 시프트, 범위 밖 정수의 signed 변환. 미지정: 몇 가지 중 하나지만 문서화 의무가 없다 — `f() + g()` 의 호출 순서. UB: 표준이 아무 요구도 하지 않는다 — signed overflow, NULL 역참조, 범위 밖 접근. 차이의 실무적 의미: 구현 정의/미지정은 "결과값이 몇 개 중 하나" 지만, UB 는 컴파일러가 **그 경로가 없다고 가정** 해서 앞뒤 코드까지 바꿀 수 있다 (`in_table` 이 테이블을 읽지도 않고 true).

</details>

<details>
<summary>Q2. 하드웨어가 2의 보수로 감기는데 signed overflow 가 왜 문제죠? 꼬리: -fwrapv 를 쓰면 되지 않나요?</summary>

"하드웨어가 무엇을 하느냐" 는 `-O0` 에서나 보인다. 최적화기는 C 추상 기계 기준으로 추론하고, 거기서 signed overflow 는 UB 라 `x + 1 > x` 를 상수 1 로, 넘침에 의존하던 루프를 `return 0` 으로 바꾼다 (실측). `-fwrapv` 는 감김을 정의된 동작으로 만들어 이 결과를 되돌린다 (`count_up` → 정확히 4). 하지만 ① 이식성이 없고(다른 컴파일러·MISRA 검사기는 모른다) ② 루프 최적화를 일부 잃고 ③ 감긴 값이 "맞는 값" 이 되는 건 아니다 — 센서 누적값이 음수로 뒤집히는 버그는 그대로다. 그래서 정답은 넘침 전에 검사하거나, 감김이 의도라면 unsigned 로 계산하는 것.

</details>

<details>
<summary>Q3. Cortex-M 에서 NULL 포인터를 읽으면 HardFault 가 나나요?</summary>

보통 아니다. 0x0000_0000 은 벡터 테이블이 있는 플래시(또는 부트 메모리 별칭)라 읽기가 성공하고 초기 MSP 값 같은 것을 돌려준다. 쓰기는 플래시라 무시되거나 버스 폴트. 그래서 ② 의 "검사 삭제" 가 리눅스 유저 공간에선 SIGSEGV 로 드러나지만 MCU 에선 조용한 오답이 된다. 대책: 검사를 역참조 앞에 두기, 커널처럼 `-fno-delete-null-pointer-checks`, 그리고 MPU 로 0번지 근처를 접근 금지 영역으로 만들어 NULL 접근을 MemManage 폴트로 바꾸기 (Zephyr 의 `CONFIG_NULL_POINTER_EXCEPTION_DETECTION_MPU` 같은 옵션). VTOR 로 벡터 테이블을 옮긴 뒤에야 가능하다.

</details>

<details>
<summary>Q4. 이 코드의 버그는? `bool in_table(int v) { for (int i = 0; i <= 4; i++) if (table[i] == v) return true; return false; }`</summary>

`table[4]` 범위 밖 읽기. 단순히 "쓰레기 값 하나 더 비교" 가 아니다 — clang 21 `-O2` 는 함수 전체를 `mov w0,#1 ; ret` 로 만들어 무엇을 넣든 true 다 (실측: 42, 5 모두 1). 추론: i = 4 까지 가면 UB → 그 전에 반드시 return true 했다. `-Wall` 은 경고하지 않고, UBSan(`index 4 out of bounds for type 'int[4]'`)과 ASan(global-buffer-overflow)이 런타임에 잡는다. 수정은 `i < 4`, 더 낫게는 크기를 `sizeof table / sizeof table[0]` 로.

</details>

<details>
<summary>Q5. 두 int32_t 의 곱이 넘치는지 int64_t 없이 검사해 보세요. 꼬리: 32비트 MCU 에선 어느 방법이 빠른가요?</summary>

부호 조합 4가지로 나눠 나눗셈으로 한계를 구한다: `a>0,b>0: a > MAX/b`, `a>0,b≤0: b < MIN/a`, `a≤0,b>0: a < MIN/b`, `a≤0,b≤0: a != 0 && b < MAX/a`. 한계 계산이 `MIN / -1` 을 만들지 않는지 확인해야 한다. 꼬리: Cortex-M4 에서 이 방법은 경로마다 `udiv`(2~12 사이클)가 들고, `(int64_t)a * b` 후 범위 검사는 `smull` 한 번이라 더 빠르다 (실측 asm). 나눗셈 방식은 입력이 이미 `int64_t`/`size_t` 처럼 가장 넓을 때 쓴다. GCC/clang 이면 `__builtin_mul_overflow`, C23 이면 `ckd_mul`.

</details>

<details>
<summary>Q6. sanitizer 를 다 켜고 테스트가 통과했으면 UB 가 없는 건가요?</summary>

아니다. ① 실행된 경로만 검사한다 (입력이 경계에 안 닿으면 못 본다 → 경계값·퍼즈 테스트가 필요한 이유) ② 못 잡는 종류가 있다: unsequenced(컴파일 경고만), strict aliasing(이 맥에선 TySan 런타임이 없음), 미초기화(MSan 은 리눅스 전용), dangling 은 ASan 옵션을 켜야 ③ 타깃 전용 코드(레지스터, ISR)는 호스트에서 안 돈다. 그리고 UBSan 은 같은 소스 위치를 한 번만 보고한다 (⑨ 에서 실제로 `/0` 이 숨었다). 그래서 경고 + 정적 분석 + sanitizer 유닛 테스트 + 리뷰를 겹쳐 쓴다.

</details>

<details>
<summary>Q7. 지역 버퍼 주소를 리턴하는 함수가 테스트에서 잘 동작했어요. 왜 위험하죠? 펌웨어에선 어떤 모양으로 나타나요?</summary>

리턴 순간 객체의 수명이 끝나고(6.2.4p2) 그 스택 칸은 다음 호출이 재사용한다. 실측: `-O0` 에선 바로 읽으면 42, 다른 함수를 부른 뒤엔 1. `-O2` 는 42 를 저장하는 명령 자체가 없었다 (죽은 저장 제거). 펌웨어 판: 지역 배열을 DMA/UART 비동기 전송에 넘기고 리턴, RTOS 큐에 지역 변수 주소를 넣고 리턴, ISR 콜백에 스택 구조체 포인터 등록. 증상은 "가끔 깨진 패킷" 이나 복귀 주소가 덮여 HardFault. 탐지는 ASan `detect_stack_use_after_return=1`, `--analyze`. 수정은 호출자 소유 버퍼, 정적 버퍼(재진입 주의), 또는 값 반환.

</details>

<details>
<summary>Q8. "UB 는 시간을 거슬러 올라간다" 는 말은 무슨 뜻인가요?</summary>

UB 가 있는 실행 전체에 표준이 요구하는 것이 없으므로, 컴파일러는 UB **이전의** 코드까지 바꿀 수 있다. `count_up` 이 예다: 넘침은 루프의 먼 미래에 일어나는데, 컴파일러는 그 사실로부터 "이 루프에 들어가는 경로(start > 0)는 없다" 고 결론 내리고 함수 첫 줄부터 `return 0` 으로 만들었다. ② 도 같은 구조다 — 역참조(뒤의 UB 가능성)가 앞선 정보(p 는 NULL 아님)가 되어 검사를 지운다. 그래서 "UB 가 일어나기 전까지는 정상 동작한다" 는 가정은 틀리다.

</details>

## 9. 추가 연습

### 연습 1 — 포화 덧셈/뺄셈

오디오·제어 루프에서는 넘치면 실패 대신 **끝값에 붙이는** 포화 산술(saturating arithmetic)을 쓴다 (Cortex-M4 DSP 명령 `QADD`/`QSUB` 가 하드웨어로 해 준다). `sat_add_i32`, `sat_sub_i32` 를 UB 없이 써라.

<details>
<summary>답 보기</summary>

Part A 의 검사를 그대로 쓰되 `return false` 대신 `INT32_MAX` / `INT32_MIN` 을 돌려준다. `sat_sub_i32(0, INT32_MIN)` 은 `INT32_MAX`. 테스트는 `int64_t` 로 계산한 뒤 clamp 한 값과 50000쌍 대조. 실무에선 CMSIS 의 `__QADD(a, b)` 가 한 명령으로 같은 일을 한다.

</details>

### 연습 2 — calloc(n, sz) 의 크기 계산

`n * sz` 가 `size_t` 에서 감기면 작은 버퍼가 할당되고 그 뒤 쓰기가 힙을 덮는다 (실제 CVE 가 많은 패턴). `add_size`, `mul_size` 를 써라. unsigned 라 UB 는 아니지만 버그다.

<details>
<summary>답 보기</summary>

`a > SIZE_MAX - b` 면 덧셈 실패, `sz != 0 && n > SIZE_MAX / sz` 면 곱셈 실패. `sz == 0` 일 때 나눗셈을 하지 않도록 순서가 중요하다. 표준 `calloc` 은 이 검사를 내부에서 하지만, 펌웨어의 자체 풀 할당자나 `malloc(n * sizeof *p)` 를 직접 쓰는 코드는 안 한다. `2^32 × 2^32` (64비트 `size_t`) 가 감겨 0 이 되는 사례를 테스트한다.

</details>

### 연습 3 — 넘치지 않는 이진 탐색

오름차순 `int32_t` 배열에서 key 의 인덱스(없으면 `n`)를 찾는 `bsearch_i32` 를 써라. 고전 버그 `mid = (lo + hi) / 2` 를 피하라.

<details>
<summary>답 보기</summary>

`size_t lo = 0, hi = n` 반열린 구간 `[lo, hi)`, `mid = lo + (hi - lo) / 2`, `a[mid] < key` 면 `lo = mid + 1`, 크면 `hi = mid`. `int` 인덱스로 `(lo + hi) / 2` 를 쓰면 원소가 2^30 개를 넘을 때 signed overflow(UB) — Java 표준 라이브러리에 2006년까지 9년간 있던 버그다. 닫힌 구간 `hi = n - 1` + `int` 를 쓰면 `n == 0` 에서 `hi = -1` 이 되고, `size_t` 면 `SIZE_MAX` 로 감긴다 — 반열린 구간이 두 문제를 다 없앤다. `n == 0` 이면 배열을 한 번도 건드리지 않으므로 `NULL` 도 안전.

</details>

### 연습 4 — 나머지와 음수 오프셋 링 인덱스

`rem_i32(a, b, &out)` 를 UB 없이, 그리고 `wrap_index(i, n)` — 어떤 `int32_t i` 든 `0..n-1` 로 접는 수학적 mod — 를 써라. 링 버퍼에서 `head - 3` 같은 음수 오프셋을 처리할 때 쓴다.

<details>
<summary>답 보기</summary>

`INT32_MIN % -1` 은 수학적으로 0 이지만 C11 6.5.5p6 에 따라 **UB** 다 (x86 은 `idiv` 가 SIGFPE). 거절 조건은 `div_i32` 와 같다. C 의 `%` 는 부호가 피제수를 따른다 (`-7 % 2 == -1`) 그래서 링 인덱스에 바로 못 쓴다. `wrap_index`: `i >= 0` 이면 `(uint32_t)i % n`, 음수면 크기 `mag = 0u - (uint32_t)i` (int → unsigned 변환은 모듈로로 **정의됨**, `INT32_MIN` 도 안전하게 2^31), `r = mag % n`, 답은 `r ? n - r : 0`. `-i` 를 signed 로 계산하면 `INT32_MIN` 에서 UB. `n` 이 2의 거듭제곱이면 `(uint32_t)i & (n - 1)` 한 줄로 같은 결과.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice    # 19/19 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o pr_san && ./pr_san   # 19/19, 보고 없음
```

## 10. 핵심 정리

- UB = 표준이 아무 요구도 하지 않음. 최적화기는 "UB 는 일어나지 않는다" 를 **전제** 로 써서 비교·검사·루프를 지운다 (`x+1>x` → 1, `<= 4` 루프 → true, 넘침 루프 → 0).
- 이 맥 실측: 9개 중 6개가 `-O0`/`-O2` 결과가 달랐다. 안 갈린 것(② 맥은 SEGV, ⑥ clang 순서 일정, ⑨ arm64 sdiv 는 트랩 없음)도 여전히 UB.
- 고치는 법은 하나: **연산·접근 전에 검사.** `a > MAX - b`, `a > MAX / b`, `b == 0 || (a == MIN && b == -1)`, `off <= cap && len <= cap - off`, NULL 검사는 역참조 앞.
- Cortex-M 에선 `*NULL` 이 폴트 없이 벡터 테이블을 읽는다 → 지워진 NULL 검사가 조용한 오답. `INT_MIN/-1` 은 x86 SIGFPE, ARM 은 조용히 감김.
- 탐지 그물: `-Wall`(③⑥), UBSan(①②④⑤⑨), ASan(④, ⑦ 옵션 필요), `--analyze`(②③⑤⑦⑨). ⑧ strict aliasing 은 이 맥에서 아무것도 못 잡았다.
- 신뢰할 수 없는 입력(패킷)은 길이 → 인덱스 → op → 산술 순서로 검사하고, 임시 변수에 계산한 뒤 성공할 때만 커밋한다.

## 참고자료

- C11 (N1570) 3.4.1/3.4.3/3.4.4 정의, 6.5p2 (unsequenced), 6.5p5 (예외 조건), 6.5p7 (aliasing), 6.5.3.2p4 (`*`), 6.5.5p5–6 (나눗셈), 6.5.6p8 (포인터 산술), 6.5.7p3–4 (시프트), 6.2.4p2 (수명), 6.3.1.3p3 (signed 변환), 6.3.2.1p2·6.7.9p10 (미초기화), 6.8.5p6 (루프 종료 가정), Annex J.2 (UB 목록) — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- John Regehr, ["A Guide to Undefined Behavior in C and C++" (Part 1–3)](https://blog.regehr.org/archives/213) — ④ `<=` 루프 예제의 출처
- Chris Lattner, ["What Every C Programmer Should Know About Undefined Behavior"](https://blog.llvm.org/2011/05/what-every-c-programmer-should-know.html) — 최적화기가 UB 를 쓰는 이유
- [SEI CERT C — INT32-C. Ensure that operations on signed integers do not result in overflow](https://wiki.sei.cmu.edu/confluence/display/c/INT32-C.+Ensure+that+operations+on+signed+integers+do+not+result+in+overflow)
- [Clang UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html), [AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html) (`detect_stack_use_after_return`)
- CVE-2009-1897 (Linux `tun` NULL 검사 삭제), Joshua Bloch, "Nearly All Binary Searches and Mergesorts are Broken" (2006)
- ARMv7-M Architecture Reference Manual — `SDIV`/`UDIV`, `CCR.DIV_0_TRP`
- 관련 레슨: C-B02 (승격·signed 넘침), C-B03 (시프트 UB 상세), C-B06 (배열 decay), C-B14 (unsequenced 분류), C-M04 (비정렬 접근), C-M13 (strict aliasing 상세), C-M15 (에러 처리·되돌리기)
