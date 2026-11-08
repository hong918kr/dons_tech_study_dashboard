---
id: C-B11
time: 50분
updated: 2026-11-07
---
# C-B11 변수는 어디에 사는가: text data bss rodata stack heap

## 학습 목표

- 선언 한 줄만 보고 그 객체가 **text / rodata / data / bss / stack / heap** 중 어디에 놓이는지 예측한다.
- 저장 기간(storage duration)과 초기화 시점을 C11 규칙으로 설명한다 — "왜 전역은 0으로 시작하고 지역은 쓰레기인가".
- macOS(ASLR 켜짐)에서 주소를 **링커 섹션 경계와 비교해서** 정직하게 분류하고, `size -m` 으로 섹션 크기 변화를 잰다.
- Cortex-M 그림을 그릴 수 있다: flash의 `.text/.rodata/.data 초기값` → RAM의 `.data`(복사) · `.bss`(0 채움) · heap · stack, 그리고 `_sidata/_sdata/_edata/_sbss/_ebss`.
- `const` 하나로 64 KB가 RAM에서 flash로 옮겨가는 이유를 숫자로 보인다.

## 1. 문제

"이 전역 배열 때문에 RAM이 넘쳤다", "부팅 직후 이 변수는 0인가?", "`const` 붙였는데 왜 RAM을 먹지?" — 펌웨어에서 메모리 배치는 곧 **flash/RAM 예산**이고, 면접에서는 "main 전에 무슨 일이 일어나나요?"로 거의 반드시 묻는다.

C 표준은 섹션(section)이라는 말을 쓰지 않는다. 표준이 정하는 건 **저장 기간** 세 가지(정적 static · 자동 automatic · 할당 allocated, C11 6.2.4 / 7.22.3)와 초기화 규칙뿐이고, 그걸 실제 메모리 영역으로 바꾸는 건 컴파일러·링커·스타트업 코드다. Cortex-M에서는 이렇게 생겼다:

```svg
<svg viewBox="0 0 720 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">Cortex-M (예: STM32F4) — 빌드 결과가 flash 와 RAM 에 놓이는 방식</text>
  <text x="40" y="50" fill="currentColor">FLASH 0x0800_0000 (읽기 전용)</text>
  <text x="430" y="50" fill="currentColor">SRAM 0x2000_0000 (읽기/쓰기)</text>
  <g stroke="currentColor">
    <rect x="40" y="60" width="230" height="34" class="f-mut"/>
    <rect x="40" y="94" width="230" height="60" class="f-acc"/>
    <rect x="40" y="154" width="230" height="40" class="f-ok"/>
    <rect x="40" y="194" width="230" height="34" class="f-warn"/>
    <rect x="40" y="228" width="230" height="40" fill="none" stroke-dasharray="4 3"/>
    <rect x="430" y="60" width="230" height="34" class="f-warn"/>
    <rect x="430" y="94" width="230" height="40" class="f-ok"/>
    <rect x="430" y="134" width="230" height="40" class="f-acc"/>
    <rect x="430" y="174" width="230" height="56" fill="none" stroke-dasharray="4 3"/>
    <rect x="430" y="230" width="230" height="44" class="f-mut"/>
  </g>
  <g fill="currentColor">
    <text x="50" y="82">.isr_vector (SP 초기값, Reset)</text>
    <text x="50" y="128">.text (코드)</text>
    <text x="50" y="179">.rodata (const, 문자열)</text>
    <text x="50" y="216">.data 초기값 ← _sidata</text>
    <text x="50" y="253">(빈 flash)</text>
    <text x="440" y="82">.data  _sdata.._edata</text>
    <text x="440" y="119">.bss  _sbss.._ebss</text>
    <text x="440" y="159">heap ↑  (_end 부터)</text>
    <text x="440" y="207">(빈 RAM)</text>
    <text x="440" y="257">stack ↓  (_estack 에서)</text>
  </g>
  <path d="M272 211 C 340 211, 360 77, 426 77" stroke="currentColor" fill="none"/>
  <path d="M418 71 L428 77 L418 83" stroke="currentColor" fill="none"/>
  <text x="300" y="150" fill="currentColor">① 복사</text>
  <text x="672" y="119" fill="currentColor">② 0</text>
  <text x="10" y="300" fill="currentColor">.data 는 주소가 둘: flash 에 실린 곳(LMA) 과 실행 중 사는 곳(VMA).</text>
  <text x="10" y="322" fill="currentColor">main 전에 Reset_Handler 가 ① 복사 ② 0 채움. stack 과 heap 은 마주 보고 자란다.</text>
  <text x="10" y="344" fill="currentColor">flash 사용량 = text + rodata + data,   RAM 사용량 = data + bss (+ heap/stack 예약분).</text>
</svg>
```

PC(macOS/Linux)에서는 OS 로더가 같은 일을 페이지 매핑으로 해준다. 이번 레슨은 **PC에서 실제 주소로 확인하고, 같은 원리를 Cortex-M 스타트업으로 옮겨** 구현한다.

### Part A — 주소 → 영역 분류기 (순수 함수)

`range_t {lo, hi, region}` 목록(메모리 맵)과 주소 하나를 받아 영역을 돌려주는 `classify(map, addr)` 를 구현한다. 범위는 **`[lo, hi)`** (lo 포함, hi 제외), 크기 0인 범위는 어떤 주소와도 안 맞는다. 테스트는 가짜 STM32F4 맵(FLASH `0x0800_0000`, SRAM `0x2000_0000`, `_estack = 0x2002_0000`)으로 경계값을 찌른다 — 결과가 실행마다 같다(결정적).

### Part B — macOS 실제 주소로 분류하기

`build_host_map()` 으로 **지금 실행 중인 내 바이너리의** 섹션 경계를 맵에 넣고, `classify_host(map, p)` 로 19개 객체(함수, 문자열 리터럴, `const` 전역, 초기값 있는/없는/0인 전역, 파일 static, 함수 안 static, 지역, `const` 지역, 지역 배열, `malloc` 블록 …)를 분류한다. ASLR 때문에 주소 숫자는 매번 바뀌지만 **영역은 매번 같아야** 한다. 실행 전에 표의 "예측" 칸을 먼저 채워 보자 (5. 풀이 Part B 표).

### Part C — Cortex-M Reset_Handler 흉내

`uint32_t flash[48]`, `uint32_t ram[16]` 를 가짜 메모리로 쓰고, 링커 심볼 역할을 하는 포인터 5개를 받아 `.data` 복사 + `.bss` 0 채우기를 하는 `reset_init()` 을 쓴다. `.bss` 끝 이후(스택 자리)는 건드리면 안 된다. 그리고 `mem_usage(text, rodata, data, bss)` 로 flash/RAM 사용량을 계산해 "64 KB 테이블에 `const` 를 빼먹으면" 무엇이 늘어나는지 숫자로 확인한다.

## 2. Problem (English)

> **Where does each object live?**
>
> 1. Implement `region_t classify(const memmap_t *m, uintptr_t addr)` that returns the region of the first range with `lo <= addr < hi`, or `R_UNKNOWN`. Zero-size ranges never match.
> 2. On a macOS/arm64 host with ASLR, build a memory map of the running executable from its Mach-O sections (`getsectiondata(&_mh_execute_header, seg, sect, &size)`) plus the main thread's stack range, and implement `classify_host()` that also recognises heap blocks (`malloc_zone_from_ptr`). Classify: a function, a string literal, `const` globals, initialised / zero / uninitialised globals, file-scope and block-scope `static`s, locals (including a `const` local), and a `malloc` block.
> 3. Write the C part of a Cortex-M `Reset_Handler`: given `_sidata, _sdata, _edata, _sbss, _ebss`, copy `.data` from flash to RAM and zero `.bss` without touching anything past `_ebss`. Then compute flash and RAM usage from section sizes.
>
> Example: on an STM32F4 map, `classify(0x20000100) == R_BSS` when `_edata == _sbss == 0x20000100`; `classify(0x20020000) == R_UNKNOWN` (`_estack` is one past the end of RAM).

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 대비
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "6/40 passed" — TODO 5곳을 채워 40/40 을 만든다
```

> Part B는 macOS 전용 API(`<mach-o/getsect.h>`, `pthread_get_stackaddr_np`, `malloc_zone_from_ptr`)를 쓴다. Linux라면 `__executable_start/etext/edata/end` 심볼과 `/proc/self/maps` 가 같은 역할을 한다.

## 4. 힌트

<details>
<summary>힌트 1 — classify 는 반열린 구간 검사 하나</summary>

`a >= lo && a < hi` 이면 그 범위다. `hi = lo + size` 이므로 size 0이면 조건이 절대 참이 될 수 없다 — 따로 처리할 필요 없음. 끝 주소를 "포함"으로 잡으면 `_edata == _sbss` 같은 경계에서 두 영역이 겹친다.

</details>

<details>
<summary>힌트 2 — 어떤 섹션이 어떤 영역인가 (직접 보기)</summary>

`size -m -l ./solution` 또는 `nm -m ./solution | grep _g_` 로 보면 된다. `__TEXT,__text` = 코드, `__TEXT,__const`·`__TEXT,__cstring`·`__DATA_CONST,__const` = 읽기 전용 데이터, `__DATA,__data` = 0 아닌 초기값, `__DATA,__bss`·`__DATA,__common` = 0 채움(zerofill). 섹션이 없으면 `getsectiondata` 는 **NULL** 을 돌려준다(그때 size 값은 믿지 말 것).

</details>

<details>
<summary>힌트 3 — 왜 &g_init 을 그냥 출력해서 비교하면 안 되나</summary>

ASLR(주소 공간 배치 무작위화)로 실행 파일 전체가 매번 다른 "슬라이드(slide)"만큼 밀린다. 링크 시점 주소(`nm` 숫자, 옛 `get_etext()`)와 실행 주소를 섞으면 틀린다. 실행 중에 **슬라이드가 반영된** 경계를 얻어야 한다 → `getsectiondata`.

</details>

<details>
<summary>힌트 4 — 스택과 힙</summary>

스택: `pthread_get_stackaddr_np(pthread_self())` 는 스택 **꼭대기(높은 주소)**, `pthread_get_stacksize_np` 는 크기 → 범위 `[top - size, top)`. 힙: 섹션이 아니라 malloc이 관리하는 영역이라, "malloc이 이 포인터를 아는가?"를 `malloc_zone_from_ptr(p) != NULL` 로 묻는다.

</details>

<details>
<summary>힌트 5 — Reset_Handler 는 while 두 개</summary>

`while (dst < end) *dst++ = *src++;` 와 `while (p < end) *p++ = 0;`. 워드(`uint32_t`) 단위로 돈다 — 링커 스크립트가 `.data/.bss` 경계를 4바이트로 정렬해 주기 때문이다.

</details>

## 5. 풀이

### Part A — 맵은 표, 분류는 검색

| 주소 | 기대 | 이유 |
|---|---|---|
| `0x0800_0000` | text | 첫 바이트 — lo 포함 |
| `0x0800_2FFF` | text | 마지막 바이트 |
| `0x0800_3000` | rodata | text 의 hi 는 제외 → 다음 범위 |
| `0x0800_3800` | ? | `_sidata`: `.data` 초기값 **이미지**. 변수가 사는 곳(VMA)이 아니다 |
| `0x2000_0100` | bss | `_edata == _sbss` — 반열린 구간이라 겹치지 않음 |
| `0x2001_FFFC` | stack | 스택 맨 위 워드 (full-descending: 첫 push 가 여기) |
| `0x2002_0000` | ? | `_estack` 은 RAM 끝 **다음** 주소. SP 초기값이지 유효한 데이터 주소가 아니다 |

주소는 `uintptr_t` 로 바꿔 비교한다. 서로 다른 객체를 가리키는 포인터끼리 `<` 로 비교하는 건 C11 6.5.8p5 기준 **UB** 이고, 포인터→정수 변환은 **구현 정의**(6.3.2.3p6)라 플랫폼이 의미를 보장한다. 실제 스타트업 코드도 사실상 이 가정 위에 서 있다.

### Part B — 선언을 보고 먼저 예측한다

```svg
<svg viewBox="0 0 720 310" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">선언 → 영역 결정 순서</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="180" height="40" rx="6" fill="none"/>
    <rect x="250" y="40" width="200" height="40" rx="6" fill="none"/>
    <rect x="250" y="120" width="200" height="40" rx="6" fill="none"/>
    <rect x="250" y="200" width="200" height="40" rx="6" fill="none"/>
    <rect x="520" y="40" width="190" height="40" rx="6" class="f-warn"/>
    <rect x="520" y="120" width="190" height="40" rx="6" class="f-ok"/>
    <rect x="520" y="200" width="90" height="40" rx="6" class="f-acc"/>
    <rect x="620" y="200" width="90" height="40" rx="6" class="f-mut"/>
    <rect x="10" y="200" width="180" height="40" rx="6" class="f-warn"/>
  </g>
  <g fill="currentColor">
    <text x="22" y="65">함수 = 코드 → text</text>
    <text x="262" y="65">블록 안 + static 없음?</text>
    <text x="532" y="65">stack (const 여도!)</text>
    <text x="262" y="145">const (정적 저장 기간)?</text>
    <text x="532" y="145">rodata (flash)</text>
    <text x="262" y="225">0 아닌 초기값?</text>
    <text x="545" y="225">data</text>
    <text x="645" y="225">bss</text>
    <text x="22" y="225">malloc/calloc → heap</text>
    <text x="460" y="58" font-size="12">예</text>
    <text x="360" y="102" font-size="12">아니오</text>
    <text x="460" y="138" font-size="12">예</text>
    <text x="360" y="182" font-size="12">아니오</text>
    <text x="470" y="218" font-size="12">예</text>
    <text x="380" y="276" font-size="12">아니오 (초기값 없음 / = 0)</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M450 60 L518 60"/><path d="M350 80 L350 118"/>
    <path d="M450 140 L518 140"/><path d="M350 160 L350 198"/>
    <path d="M450 220 L518 220"/><path d="M350 240 L350 260 L665 260 L665 242"/>
  </g>
  <text x="10" y="296" fill="currentColor">예외: 포인터 배열은 포인터까지 const 여야 rodata (const char *const t[]).</text>
</svg>
```

먼저 예측을 적고 실행 결과와 비교하자. 실제 출력(`./solution`, 주소는 실행마다 다름):

```text
  object                                  address  region
  main (function)                     0x1040c0638  text
  "literal" (string lit)              0x1040c21ba  rodata
  g_const                             0x1040c1eb0  rodata
  g_lut[256] (const)                  0x1040c1eb4  rodata
  g_names[] (const ptr[])             0x1040c4060  rodata
  local_tbl (static const)            0x1040c1fb4  rodata
  g_init = 42                         0x1040c8000  data
  s_file = 5 (static)                 0x1040c8010  data
  &g_msg (the pointer)                0x1040c8008  data
  g_msg (points to)                   0x1040c1fc4  rodata
  seed = 1234 (fn static)             0x1040c8014  data
  g_zero = 0                          0x1040c8018  bss
  g_uninit                            0x1040c801c  bss
  s_zero (static)                     0x1040c8028  bss
  calls (fn static)                   0x1040c802c  bss
  local                               0x16bd3e5c4  stack
  local_k (const local)               0x16bd3e5c0  stack
  local_arr[] = "hi"                  0x16bd3e5bc  stack
  &local_ptr                          0x16bd3e5b0  stack
  local_ptr (points to)               0x1040c2190  rodata
  malloc(32)                          0x1045d9150  heap
40/40 passed
```

| 선언 | 영역 | 초기화 시점 / 규칙 |
|---|---|---|
| `int g_init = 42;` | data | 프로그램 시작 **전** (C11 6.2.4p3). 값이 이미지에 실려 있다 |
| `int g_zero = 0;` / `int g_uninit;` | bss | 명시 초기값 없는 정적 객체는 0 (6.7.9p10). 0은 파일에 실을 필요가 없다 |
| `static int s_file = 5;` | data | `static` 은 링키지만 바꾼다(C-B10). 위치는 초기값이 결정 |
| `{ static int seed = 1234; }` | data | **첫 호출 때가 아니라 시작 전** 초기화. C는 초기값이 상수식이어야 한다(6.7.9p4) — C++ 의 "첫 통과 시 초기화"와 다르다 |
| `const int g_const = 7;` | rodata | 수정 시도는 UB (6.7.3p6). Mac: `__TEXT` 는 읽기 전용 페이지 → 시그널, MCU: flash 쓰기는 무시되거나 버스 폴트 |
| `"literal"` | rodata | 정적 저장 기간 배열 (6.4.5p6), 수정은 UB (6.4.5p7) |
| `const char *g_msg = "hello";` | 포인터=data, 내용=rodata | 포인터 변수는 바꿀 수 있으니 data. 둘은 서로 다른 객체 |
| `const char *const g_names[]` | rodata (Mac은 `__DATA_CONST`) | 아래 "재배치" 참고 |
| `int local;` / `const int local_k = 5;` | stack | 자동 저장 기간. 초기값 없으면 **불확정 값**(indeterminate). `const` 는 "이 이름으로 못 바꿈"이지 "flash에 둠"이 아니다 |
| `char local_arr[] = "hi";` | stack | 리터럴을 매 호출마다 스택 배열로 **복사** |
| `malloc(32)` | heap | 할당 저장 기간. 내용은 불확정 (calloc 이면 0) |

macOS가 실제로 쓰는 섹션 이름은 ELF와 다르다. `nm -m ./solution` 실측:

```text
0000000100008000 (__DATA,__data) external _g_init
000000010000801c (__DATA,__common) external _g_uninit
0000000100008018 (__DATA,__common) external _g_zero
0000000100008028 (__DATA,__bss) non-external _s_zero
000000010000802c (__DATA,__bss) non-external _counter_addr.calls
0000000100001eb0 (__TEXT,__const) external _g_const
0000000100004060 (__DATA_CONST,__const) external _g_names
0000000100001fb4 (__TEXT,__const) non-external _main.local_tbl
```

```svg
<svg viewBox="0 0 720 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">macOS arm64 프로세스 (낮은 주소 → 높은 주소) 와 ELF 대응</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="120" height="44" class="f-mut"/>
    <rect x="130" y="40" width="160" height="44" class="f-acc"/>
    <rect x="290" y="40" width="100" height="44" class="f-ok"/>
    <rect x="390" y="40" width="130" height="44" class="f-warn"/>
    <rect x="520" y="40" width="80" height="44" fill="none" stroke-dasharray="4 3"/>
    <rect x="600" y="40" width="110" height="44" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="18" y="58">__PAGEZERO</text><text x="18" y="76">4 GB, 접근 불가</text>
    <text x="138" y="58">__TEXT (r-x)</text><text x="138" y="76">text·const·cstring</text>
    <text x="298" y="58">__DATA_CONST</text><text x="298" y="76">재배치 후 r--</text>
    <text x="398" y="58">__DATA (rw-)</text><text x="398" y="76">data·common·bss</text>
    <text x="528" y="58">heap…</text><text x="528" y="76">malloc</text>
    <text x="608" y="58">stack ↓</text><text x="608" y="76">0x16b…, 8 MB</text>
  </g>
  <path d="M130 100 L390 100" stroke="currentColor"/><path d="M382 95 L390 100 L382 105" stroke="currentColor" fill="none"/>
  <text x="140" y="120" fill="currentColor">전체가 실행마다 같은 slide 만큼 이동 (ASLR). 섹션 사이 간격은 고정.</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="10" y="160">Mach-O                       ELF (Linux / arm-none-eabi)</text>
    <text x="10" y="182">__TEXT,__text                .text</text>
    <text x="10" y="202">__TEXT,__const / __cstring    .rodata / .rodata.str1.1</text>
    <text x="10" y="222">__DATA_CONST,__const         .data.rel.ro  (PIC 일 때만)</text>
    <text x="10" y="242">__DATA,__data                .data</text>
    <text x="10" y="262">__DATA,__bss / __common      .bss  (COMMON: -fcommon 일 때)</text>
  </g>
  <text x="10" y="295" fill="currentColor">zerofill 섹션은 파일에 바이트가 없다 — 로더가 0 페이지를 매핑 (MCU 는 Reset_Handler 가 0 기록).</text>
  <text x="10" y="317" fill="currentColor">__DATA_CONST: 포인터 값은 slide 를 더해 고쳐야 하므로(rebase) 로딩 후에야 읽기 전용이 된다.</text>
</svg>
```

관찰 포인트 세 가지:

1. **`__common` vs `__bss`**: Apple clang 드라이버는 Darwin에서 기본으로 `-fcommon` 을 넘긴다(`cc -### -c solution.c` 에 `"-fcommon"` 이 보임). 외부 링키지 0 초기화 전역은 `__DATA,__common`, `static` 은 `__DATA,__bss` 로 갔다. 둘 다 zerofill = 개념상 bss. (Linux의 GCC 10+/Clang 11+ 는 기본 `-fno-common` → 전부 `.bss`.) 그래서 분류기에 두 섹션을 모두 넣었다. 어떤 binary엔 `__bss` 가 아예 없다 — 이때 `getsectiondata` 는 NULL.
2. **`__DATA_CONST`**: `g_names` 는 포인터 배열이라 값이 "문자열 주소"다. PIE + ASLR에선 그 주소가 로딩 때 정해지므로 dyld가 고쳐 쓴 뒤 읽기 전용으로 바꾼다(ELF의 `.data.rel.ro`). 절대 주소로 링크하는 Cortex-M 펌웨어에선 그냥 `.rodata` 다 (7. 실수 표 참고).
3. **ASan 빌드**: `-fsanitize=address` 로 빌드하면 문자열 리터럴이 `__TEXT,__asan_cstring` 으로 옮겨간다. 처음엔 이걸 몰라서 ASan 빌드에서 리터럴 3개가 `?` 로 나왔다 → 섹션을 하나 추가해 해결. 섹션 이름은 **툴체인 세부사항**이라 바뀔 수 있다는 교훈.

**정직하지 않은 방법 (해 봤고 안 됨)**: 옛 UNIX식 `get_etext()/get_edata()/get_end()` 는 macOS 13부터 deprecated이고, 반환값이 **링크 시점 주소**다. `demo_etext.c` 를 두 번 실행한 결과:

```text
get_etext() = 0x10000059c   get_edata() = 0x100008004   get_end() = 0x100010000   (링크 시점 주소)
&_mh_execute_header = 0x1044d0000   main = 0x1044d04f8   &g_init = 0x1044d8000   (실제 주소)
getsectiondata(__DATA,__data) = 0x1044d8000   (슬라이드 반영됨)
get_etext() = 0x10000059c   get_edata() = 0x100008004   get_end() = 0x100010000   (링크 시점 주소)
&_mh_execute_header = 0x100f54000   main = 0x100f544f8   &g_init = 0x100f5c000   (실제 주소)
getsectiondata(__DATA,__data) = 0x100f5c000   (슬라이드 반영됨)
```

`get_*` 값은 두 번 다 같고 실제 주소와 슬라이드(`0x44d0000`, `0xf54000`)만큼 어긋난다. `-Werror` 빌드에선 deprecated 경고가 에러라 본 문제 코드에는 쓰지 않았다.

**섹션 크기 측정 (`size_demo.c` + `sh size_demo.sh`)** — 64 KB 배열 하나를 선언 방식만 바꿔 빌드:

```text
=== VARIANT=0  (macOS)  file size: 16832 bytes
	Section __text: 4
=== VARIANT=1  (macOS)  file size: 82928 bytes
	Section __text: 20
	Section __data: 65536
ld: warning: reducing alignment of section __DATA,__common from 0x8000 to 0x4000 because it exceeds segment maximum alignment
=== VARIANT=2  (macOS)  file size: 16880 bytes
	Section __text: 20
	Section __common: 65536 (zerofill)
=== VARIANT=3  (macOS)  file size: 82912 bytes
	Section __text: 20
	Section __const: 65536
=== VARIANT=0  (Cortex-M4 ELF .o)
.text                  2      0
=== VARIANT=1  (Cortex-M4 ELF .o)
.text                  14      0
.data               65536      0
=== VARIANT=2  (Cortex-M4 ELF .o)
.text                  14      0
.bss                65536      0
=== VARIANT=3  (Cortex-M4 ELF .o)
.text                  14      0
.rodata             65536      0
```

| 선언 | 섹션 | 파일(=flash 이미지) 증가 | 실행 시 RAM 증가 |
|---|---|---|---|
| `uint8_t big[65536] = {1};` | data | **+64 KB** (초기값을 실어야 함) | **+64 KB** |
| `uint8_t big[65536];` | bss/common | +48 B (사실상 0) | +64 KB |
| `const uint8_t big[65536] = {1};` | rodata | +64 KB | **0** |

- ELF 쪽은 `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -c` 로 만든 **오브젝트 파일**을 `llvm-size -A` 로 본 것이다(맥에 ARM ELF 링커가 없어 링크는 안 함). 섹션 배치를 보는 데는 충분하다. 링크된 펌웨어의 map 파일 읽기는 TOOL-B06, TOOL-M02, BM-B05 에서 한다.
- 맥의 기본 `size`(옵션 없음)는 세그먼트 크기를 16 KB 페이지 단위로 보여줘서 VARIANT 1과 2가 둘 다 `__DATA 65536` 으로 나온다 → 차이를 보려면 반드시 `size -m`.
- `ld: warning` 은 큰 배열에 컴파일러가 준 32 KB 정렬을 링커가 16 KB(세그먼트 최대)로 낮췄다는 뜻 — 동작에는 영향 없음.
- 이 맥의 `llvm-size` 는 아주 오래된 버전(3.2)이라 Berkeley 형식(`text data bss`)에서 `.rodata` 를 data 칸에 넣는다. GNU `arm-none-eabi-size` 는 읽기 전용 섹션을 **text 칸**에 넣는다. 그래서 여기선 섹션별로 보이는 `-A` 만 믿었다.

### Part C — main 전에 일어나는 일

STM32CubeIDE의 `startup_stm32f4xx.s` 가 하는 일을 C로 옮기면 정확히 이 두 줄이다:

```c
while (sdata < edata) *sdata++ = *sidata++;   /* ① .data: flash(LMA) → RAM(VMA) */
while (sbss < ebss)   *sbss++ = 0;            /* ② .bss: 0 채우기               */
```

전체 순서(최근 STM32Cube GCC 템플릿 기준)는 ① 리셋 시 하드웨어가 벡터 테이블 0번 워드를 SP에(`_estack`), 1번 워드를 PC에(`Reset_Handler`) 로드 ② `SystemInit()`(클럭·FPU 등) ③ `.data` 복사 ④ `.bss` 0 채움 ⑤ `__libc_init_array()`(생성자) ⑥ `main()`. 그래서 **`.data`/`.bss` 가 준비되기 전에 도는 `SystemInit` 안에서는 전역 변수를 믿으면 안 된다** — 면접 꼬리질문 단골. (예전 템플릿·다른 벤더는 복사 후 `SystemInit` 을 부르기도 한다 — 내 프로젝트의 startup 파일을 직접 확인.) (C로 startup 전체를 쓰는 건 BM-B08, 링커 스크립트의 `AT> FLASH`·`LOADADDR` 문법은 TOOL-M01.)

테스트가 확인하는 것:

| 검사 | 의미 |
|---|---|
| `ram[0..3] == data_img` | 초기값 복사 |
| `ram[4..9] == 0` | 전원 직후 쓰레기(`0xDEADBEEF`)였던 bss가 0 |
| `ram[10..15] == 0xDEADBEEF` | `_ebss` 이후(힙·스택 자리)는 안 건드림 |
| 빈 `.data`/`.bss` 호출 | `sdata == edata` 면 루프 0회 |

`mem_usage`: flash = text + rodata + data (`.data` 초기값이 flash에 실린다!), RAM = data + bss. 64 KB 테이블에 `const` 를 빼먹으면 **flash도 RAM도** 64 KB 늘고, `const` 를 붙이면 RAM 증가는 0 — 128 KB RAM MCU에서 이건 링크 실패(`region 'RAM' overflowed`)냐 아니냐의 차이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 40/40 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o sol_asan && ./sol_asan  # 40/40, 리포트 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `const char *names[] = {"a","b"};` 를 "상수 테이블"이라 생각 | 포인터 자체는 const가 아님 → `.data` (RAM + flash 둘 다). Cortex-M ELF 실측: `names_a` → `D`(.data) | `const char *const names[]` → `R`(.rodata) |
| `const int k = 5;` 지역 변수가 flash에 있다고 답함 | 자동 저장 기간 → stack. 실측 `local_k` = stack | 정말 flash에 두려면 `static const` |
| 큰 지역 배열 `uint8_t buf[4096] = {0};` | 매 호출마다 스택 4 KB + 0 채우기 시간. 작은 태스크 스택이면 오버플로 | `static` 으로 bss 에 두거나 크기 줄이기 (BM-A07 스택 측정) |
| 링크 시점 주소(`nm`, `get_etext()`)와 실행 주소를 비교 | ASLR 슬라이드만큼 어긋남 | `getsectiondata` 처럼 슬라이드 반영된 값 사용 |
| `getsectiondata` 반환값 NULL 무시 | 섹션이 없으면 NULL, size 는 이전 값이 남아 있을 수 있음 (실제로 `__bss` 없는 binary에서 겪음) | `if (p) map_add(...)` |
| 범위 끝을 `<=` 로 비교 | `_edata == _sbss` 경계 주소가 두 영역에 동시에 속함 | `lo <= a && a < hi` |
| -O2 에서 `static int s = 5;` 가 `nm` 에 안 보임 | 한 번도 안 쓰이고 주소도 안 넘기면 상수로 접혀 사라짐 (ELF `.o` 실측에서 `s_file` 증발) | 관찰하려면 주소를 넘기거나 값을 바꾼다 |
| `SystemInit()` 안에서 전역 변수 사용 | `.data` 복사·`.bss` 0 채움 **전**에 실행됨 | 지역 변수/레지스터만 쓰거나, 초기화 후로 옮김 |
| 펌웨어에서 `.bss` 변수가 리셋 후에도 남길 기대 | Reset_Handler 가 매번 0으로 지움 | 링커 스크립트에 `.noinit` 섹션 (TOOL-A07) |

## 8. 면접 Q&A

<details>
<summary>Q1. .data 와 .bss 는 왜 나뉘어 있나요?</summary>

둘 다 정적 저장 기간의 쓰기 가능한 변수지만, `.bss` 는 "전부 0"이라 **초기값을 이미지에 실을 필요가 없다**. 실측: 64 KB 0 배열은 파일 크기를 48 B만 늘렸고, 초기값 1개짜리 64 KB 배열은 64 KB를 늘렸다. MCU에선 그만큼 flash 절약. 실행 시 RAM은 둘 다 차지한다. 꼬리질문 "`int x = 0;` 은 어디?" → bss (컴파일러가 0 초기값을 bss로 보냄. 단 `-fno-zero-initialized-in-bss` 같은 옵션이 있으면 data).

</details>

<details>
<summary>Q2. Cortex-M 에서 리셋부터 main 까지 무슨 일이 일어나나요?</summary>

하드웨어가 벡터 테이블의 0번 워드로 MSP, 1번 워드(Reset_Handler 주소, Thumb 비트 1)로 PC를 설정. Reset_Handler: (SP 설정) → `SystemInit()` → `_sidata`→`_sdata.._edata` 복사 → `_sbss.._ebss` 0 → `__libc_init_array()` → `main()`. 심볼은 링커 스크립트가 제공한다 (`_sidata = LOADADDR(.data)`). 꼬리질문 "main 이 return 하면?" → bare-metal엔 돌아갈 OS가 없다. 잘 만든 스타트업은 무한 루프로 막지만, 그냥 `bx lr` 로 끝나는 템플릿도 있어 정의되지 않은 곳으로 뛰고 폴트가 난다 → main 은 return 하지 않게 짠다.

</details>

<details>
<summary>Q3. const 를 붙이면 항상 flash(.rodata)에 들어가나요?</summary>

아니다. ① 지역 `const` 는 스택 ② `const char *t[]` 는 포인터가 const가 아니라 `.data` ③ PIC/PIE면 재배치가 필요한 const는 `.data.rel.ro`(Mac `__DATA_CONST`) ④ `const volatile` 은 하드웨어가 바꾸는 읽기 전용 레지스터 표현이지 위치 지정이 아니다. 그리고 const 객체를 캐스트로 고쳐 쓰는 것은 UB (6.7.3p6). 일부 MCU 툴체인(예: AVR)은 `const` 만으론 flash에 안 가고 `PROGMEM` 이 필요하다 — Harvard 구조라서.

</details>

<details>
<summary>Q4. 함수 안의 static 변수는 언제 초기화되나요? C++ 과 같나요?</summary>

C: 프로그램 시작 **전에 한 번** (정적 저장 기간, 초기값은 상수식이어야 함 6.7.9p4). 그래서 비용 0, 스레드 문제 없음 — `.data`/`.bss` 에 그냥 있다. C++: 상수 초기화가 아니면 **처음 선언을 지날 때** 초기화되고, C++11부터 그 초기화는 스레드 안전(가드 변수 + 락 → 임베디드에서 `__cxa_guard_acquire` 비용, `-fno-threadsafe-statics` 로 끄기도 함).

</details>

<details>
<summary>Q5. 스택 오버플로는 어떻게 찾나요? 힙과 스택이 부딪히면?</summary>

스택 페인팅(시작 시 패턴으로 채우고 high-water mark 측정, FreeRTOS `uxTaskGetStackHighWaterMark`), MPU 가드 영역(스택 바닥에 접근 금지 영역 → MemManage fault), ARMv8-M의 `MSPLIM/PSPLIM` 하드웨어 한도 레지스터, `-fstack-protector` 카나리, `-fstack-usage` + 호출 그래프 정적 분석. 힙/스택 충돌: newlib `_sbrk` 가 현재 SP 근처까지 힙을 늘리려 하면 실패를 돌려주도록 구현한다 (추가 연습 3·4).

</details>

<details>
<summary>Q6. 펌웨어에서 malloc 을 피하는 이유는?</summary>

단편화(fragmentation)로 장시간 운전 후 실패, 실행 시간이 비결정적(실시간성 저해), 실패 처리 경로가 테스트되기 어려움, 힙/스택 충돌을 정적으로 증명 못 함. MISRA C:2012 Rule 21.3 도 `<stdlib.h>` 동적 할당을 금지. 대안: 정적 할당, 고정 크기 메모리 풀, 초기화 단계에서만 할당하고 이후 금지.

</details>

<details>
<summary>Q7. char *p = "abc"; 와 char a[] = "abc"; 의 차이는?</summary>

`p` 는 포인터 변수(전역이면 data, 지역이면 stack)이고 리터럴(rodata)을 가리킨다 — `p[0] = 'x'` 는 UB(Mac에선 읽기 전용 페이지라 크래시). `a` 는 4바이트 배열 자체이고 리터럴로 **초기화된 복사본** — 수정 가능, `sizeof a == 4`, `sizeof p == 8`(arm64) / 4(Cortex-M). 실측: `local_arr` = stack, `local_ptr` 가 가리키는 곳 = rodata.

</details>

<details>
<summary>Q8. 큰 전역 배열을 .bss 에 두면 공짜인가요?</summary>

flash는 공짜지만 **부팅 시간**은 아니다 — Reset_Handler 가 전부 0으로 써야 한다(수십~수백 KB면 ms 단위). 리셋 후 값 보존이 필요하거나 0 채움이 필요 없는 버퍼(DMA 버퍼, 크래시 로그)는 `.noinit` 섹션으로 빼서 스타트업 루프에서 제외한다.

</details>

## 9. 추가 연습

### 연습 1 — 선언 규칙 엔진

`where_lives(file_scope, is_static, is_const, init)` 가 영역을 돌려주게 하라. `{ const int k = 1; }` 와 `{ static int n; }` 를 포함한 7가지 경우를 테스트.

<details>
<summary>답 보기</summary>

자동 저장 기간(블록 안 + static 없음)이면 무조건 stack → 정적이면 const 는 rodata → 0 아닌 초기값이면 data, 아니면 bss. 5. 풀이 Part B 의 흐름도 그대로다. (포인터 배열의 const 함정은 이 단순 규칙 밖 — 별도로 기억.)

</details>

### 연습 2 — 스택 성장 방향

호출된 함수의 지역 변수 주소가 호출한 함수의 것보다 낮은지로 `stack_grows_down()` 을 구현하라. 인라인되면 실험이 깨진다.

<details>
<summary>답 보기</summary>

`volatile` 함수 포인터로 호출해 인라인을 막고, 비교는 **callee 안에서** 한다 (지역 주소를 return 하면 clang `-Wreturn-stack-address` 가 `-Werror` 에서 에러 — 실제로 겪음). 주소는 `uintptr_t` 로 비교. arm64 macOS·Cortex-M 모두 full-descending 이라 true.

</details>

### 연습 3 — 스택 페인팅과 high-water mark

64워드 태스크 스택을 `0xA5A5A5A5` 로 칠하고, 10·40·5 워드 사용 후 "한 번도 안 쓰인 워드 수"를 구하라.

<details>
<summary>답 보기</summary>

스택은 꼭대기에서 아래로 쓰이므로 **바닥(인덱스 0)부터** 페인트가 남아 있는 워드를 센다 → 64 − 40 = 24. 현재 사용량(5)이 아니라 **최대** 사용량이 남는다는 게 핵심. FreeRTOS 의 high-water mark 가 이 방식이다 (BM-A07 에서 실제 MCU로).

</details>

### 연습 4 — `_sbrk` 흉내: 힙·스택 충돌 검사

`sram[1024]` 의 앞에서 힙이 위로, 끝에서 스택이 아래로 자란다. `sbrk_sim(incr, guard)` 는 힙 끝과 현재 SP 사이에 `incr + guard` 바이트가 없으면 NULL.

<details>
<summary>답 보기</summary>

`gap = fake_sp - heap_end` (둘 다 같은 배열 안이라 포인터 뺄셈이 정의됨) 를 `incr + guard` 와 비교. 실패 시 `heap_end` 는 그대로. newlib 의 `_sbrk` (STM32Cube `sysmem.c`) 가 `_end` 와 `_estack - _Min_Stack_Size` 로 같은 검사를 한다.

</details>

### 연습 5 — size 출력 → 사용률

`arm-none-eabi-size` 가 `text=48212 data=1236 bss=20480` 을 찍었다. STM32F401(flash 256 KB, RAM 64 KB)에서 사용률(0.1 % 단위)은?

<details>
<summary>답 보기</summary>

GNU size의 Berkeley 형식에서 **text 칸에 이미 .rodata·.isr_vector 가 포함**되어 있다. flash = text + data = 49448 → 18.8 %, RAM = data + bss = 21716 → 33.1 %. (STM32CubeIDE 기본 링커 스크립트에선 `._user_heap_stack` 예약분도 bss 칸에 잡힌다.) 곱셈은 `uint64_t` 로 넓혀 오버플로 방지.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 17/17 passed
```

데모 파일 (관찰용, 하네스 아님):

```bash
sh size_demo.sh                                                                     # size_demo.c 를 4가지로 빌드해 섹션 크기 비교
cc -std=c11 -O2 -Wno-deprecated-declarations demo_etext.c -o demo_etext && ./demo_etext   # get_etext() 는 링크 시점 주소
```

## 10. 핵심 정리

- 함수 → text, `const` 정적 객체·문자열 리터럴 → rodata, 0 아닌 초기값 정적 객체 → data, 초기값 없음/0 → bss, 지역(`const` 포함) → stack, `malloc` → heap.
- 정적 저장 기간 객체는 **main 전에** 초기화되고 명시 초기값 없으면 0. 자동 객체는 불확정 값.
- flash = text + rodata + data(초기값 이미지), RAM = data + bss + heap + stack. **bss는 flash를 안 먹는다.**
- `const char *const tbl[]` — 포인터까지 const 여야 rodata.
- Cortex-M Reset_Handler: `_sidata → _sdata.._edata` 복사, `_sbss.._ebss` 0, 그 다음 main. `SystemInit` 은 그 전에 돈다.
- macOS 실측은 ASLR 때문에 **슬라이드 반영된 섹션 경계**(`getsectiondata`)로 분류, 크기는 `size -m`. Mach-O 이름(`__TEXT,__const`, `__DATA,__common`)은 ELF(`.rodata`, `.bss`)로 번역해서 읽는다.

## 참고자료

- [C11 표준 초안 N1570](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf) — 6.2.4 저장 기간, 6.4.5 문자열 리터럴, 6.7.3 const, 6.7.9 초기화, 6.9.2 tentative definition
- [GNU ld 매뉴얼 — Output Section LMA (`AT>`)](https://sourceware.org/binutils/docs/ld/Output-Section-LMA.html)
- [STMicroelectronics cmsis_device_f4 — GCC startup 템플릿 (`startup_stm32f4xx.s`)](https://github.com/STMicroelectronics/cmsis_device_f4)
- [Arm Cortex-M4 Devices Generic User Guide — 벡터 테이블과 리셋 동작](https://developer.arm.com/documentation/dui0553/latest/)
- macOS man 페이지: `man 3 getsectiondata`, `man size`, `man nm` (`-m` 옵션)
- 관련 레슨: C-B08 (const 와 .rodata), C-B10 (static 과 링키지), TOOL-B06 / TOOL-M01 / TOOL-M02 (size·링커 스크립트·map 파일), BM-B05 / BM-B08 / BM-A07
