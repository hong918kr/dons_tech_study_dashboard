---
id: C-B10
time: 50분
updated: 2026-11-07
---
# C-B10 static과 Linkage: 파일 스코프·함수 내 static·extern

## 학습 목표

- 선언 하나를 보고 **범위(scope) · 연결(linkage) · 저장 기간(storage duration)** 세 가지를 따로 말할 수 있다.
- `static` 이 파일 스코프에서는 **연결을 internal로**, 함수 안에서는 **저장 기간을 static으로** 바꾼다는 차이를 설명한다.
- `nm` 출력의 `T/t/D/d/B/b/C/S/U` 를 읽고, Mach-O(맥)와 ELF(arm-none-eabi)의 차이를 안다.
- `duplicate symbol` 링크 에러를 재현하고 `static` 으로 고치며, **tentative definition** 과 `-fcommon`/`-fno-common` 의 관계를 설명한다.
- 함수 내 static 으로 init-once · 카운터 · 모듈 상태 숨기기를 구현하고, 그 대가(재진입 불가, ISR 공유)를 안다.

## 1. 문제

펌웨어는 수십 개 `.c` 파일이 하나의 이미지로 링크된다. "이 변수/함수가 어느 파일까지 보이나, 언제 생기고 언제 사라지나"를 모르면 `duplicate symbol`, 헤더 때문에 생긴 **변수 사본 두 개**, ISR에서 깨지는 static 버퍼 같은 버그를 만든다 — 그래서 면접에서 "static의 의미 세 가지"는 단골 질문이다.

C의 이름에는 세 가지 속성이 따로 있다. 헷갈리는 이유는 `static` 한 단어가 **위치에 따라 다른 속성을 바꾸기** 때문이다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">3파일 프로젝트의 심볼 지도 (proj/ 를 nm 으로 본 결과)</text>
  <g stroke="currentColor" fill="none">
    <rect x="10" y="40" width="230" height="200" rx="8"/>
    <rect x="255" y="40" width="230" height="200" rx="8"/>
    <rect x="500" y="40" width="230" height="200" rx="8"/>
  </g>
  <rect x="20" y="66" width="210" height="80" class="f-ok"/>
  <rect x="20" y="152" width="210" height="80" class="f-mut"/>
  <rect x="265" y="66" width="210" height="60" class="f-ok"/>
  <rect x="265" y="152" width="210" height="50" class="f-mut"/>
  <rect x="510" y="66" width="210" height="166" class="f-acc"/>
  <g fill="currentColor" font-weight="bold">
    <text x="20" y="58">uart.o</text><text x="265" y="58">gpio.o</text><text x="510" y="58">main.o</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="28" y="82">T uart_init  T uart_send</text>
    <text x="28" y="100">T uart_baud</text>
    <text x="28" y="118">D uart_log_level</text>
    <text x="28" y="136">C uart_tx_bytes</text>
    <text x="28" y="168">t hw_reset   t hw_putc</text>
    <text x="28" y="186">d s_baud     b s_rxbuf</text>
    <text x="28" y="204">b uart_init.calls</text>
    <text x="28" y="222">b uart_init.done</text>
    <text x="273" y="82">T gpio_init</text>
    <text x="273" y="100">T gpio_write</text>
    <text x="273" y="118">T gpio_state</text>
    <text x="273" y="168">t hw_reset</text>
    <text x="273" y="186">b s_odr</text>
    <text x="518" y="82">T main</text>
    <text x="518" y="104">U uart_init  U uart_send</text>
    <text x="518" y="122">U uart_baud</text>
    <text x="518" y="140">U uart_tx_bytes</text>
    <text x="518" y="158">U uart_log_level</text>
    <text x="518" y="176">U gpio_init  U gpio_write</text>
    <text x="518" y="194">U gpio_state U printf</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="440" y="100" text-anchor="end">외부 연결</text>
    <text x="440" y="190" text-anchor="end">static = 내부</text>
  </g>
  <path d="M500 130 L240 110" stroke="currentColor" fill="none" stroke-dasharray="5 3"/>
  <path d="M500 180 L485 100" stroke="currentColor" fill="none" stroke-dasharray="5 3"/>
  <text x="10" y="264" fill="currentColor">링커는 main.o 의 U(미정의)를 다른 .o 의 <tspan font-weight="bold">대문자</tspan> 심볼(T/D/C)로만 채운다.</text>
  <text x="10" y="288" fill="currentColor">소문자(t/d/b)는 자기 .o 안에서만 쓰인다 → 두 파일의 hw_reset 이 충돌 없이 공존한다.</text>
</svg>
```

### Part A — 함수 내 static 카운터

시작 코드의 세 함수는 지역 변수가 **자동(automatic)** 이라 호출마다 0부터 다시 시작한다. 키워드 하나로 고쳐라.

- `seq_next()` — 호출할 때마다 1, 2, 3, …
- `other_next()` — 101, 102, … (`seq_next` 의 `seq` 와 이름이 같아도 **별개 객체**여야 한다)
- `first_time()` — 처음 호출에서만 `true`

### Part B — init-once 지연 초기화와 정적 버퍼의 함정

- `crc8_table()` — CRC-8 256칸 테이블을 **처음 요청될 때 딱 한 번** 만들고 그 주소를 돌려준다. `g_table_builds` 는 1이어야 한다. (부팅 시간을 줄이려고 펌웨어에서 자주 쓰는 lazy init)
- `hex32()` (주어짐) 은 static 버퍼를 돌려준다. 두 번 부르면 **첫 결과가 덮어써지는 것**을 테스트가 보여준다. 호출자가 버퍼를 주는 재진입 가능(reentrant) 버전 `hex32_r(v, out)` 을 구현하라.

### Part C — static으로 상태를 숨긴 UART 모듈 + 3파일 링크 실험

코드: 한 파일 안에 "uart.h(공개 API)" 와 "uart.c(static 상태)" 를 흉내 낸 모듈을 완성한다.

- `uart_init()` — 첫 호출만 `true`, 두 번째부터 `false` (init-once)
- `uart_putc(c)` — 8칸 송신 큐에 넣기. 미초기화면 `false`, 가득 차면 `s_dropped++` 후 `false`
- `uart_tx_isr()` — TXE 인터럽트 흉내: 큐에서 1바이트를 `hw_write_tdr()` 로 보내고 `uart_bytes_sent++`
- `uart_dropped()` — 버린 바이트 수

실험: `proj/` 의 진짜 여러 파일 프로젝트(`main.c uart.c uart.h gpio.c gpio.h`)를 빌드해 `nm` 으로 심볼을 보고, **이름 충돌**과 **tentative definition 충돌**을 재현·해결한다 (풀이 Part C에 명령과 실제 출력).

## 2. Problem (English)

> **Linkage and storage duration in a multi-file firmware project.**
>
> 1. Fix `seq_next()`, `other_next()`, `first_time()` so that state survives between calls. Two functions may each have a local named `seq`; they must not share it.
> 2. Implement `const uint8_t *crc8_table(void)` that builds a 256-entry CRC-8 (poly 0x07) table on first use only and returns a pointer that stays valid after return. Implement `char *hex32_r(uint32_t v, char out[11])` — a reentrant replacement for a formatter that returns a static buffer.
> 3. Implement a UART TX module whose state (`s_txq`, `s_head`, `s_tail`, `s_ready`, `s_dropped`) is invisible to other translation units. Public API: `uart_init` (init-once), `uart_putc` (8-entry queue, count drops), `uart_tx_isr` (drain one byte per interrupt), `uart_dropped`.
> 4. Given `uart.c` and `gpio.c` that both define a helper `hw_reset()`, explain the linker error you get without `static`, read the `nm` output, and explain what `int debug_flags;` in two files does under `-fcommon` vs `-fno-common`.
>
> Example: `crc8("123456789", 9) == 0xF4`; after `hex32(0xDEADBEEF)` then `hex32(0x1234)`, the first pointer now reads `"0x00001234"`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "11/32 passed" — TODO를 채워 32/32 을 만든다
```

> 맥 SDK 27.2에서 `ld: tapi error` 가 나면 `export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk` 후 다시 빌드.

여러 파일 실험 (`proj/` 디렉터리):

```bash
cd proj
cc -std=c11 -Wall -Wextra -Werror -O0 -c main.c uart.c gpio.c   # .o 3개
nm uart.o ; nm gpio.o ; nm main.o                                # 심볼 보기
cc main.o uart.o gpio.o -o app && ./app                          # 링크·실행
cc -std=c11 -Wall -Wextra -Werror -O0 -DDEMO_COLLISION -c uart.c gpio.c && cc main.o uart.o gpio.o -o app   # 충돌
cc -std=c11 -Wall -Wextra -Werror -O0 -fno-common -DDEMO_TENTATIVE -c uart.c gpio.c && cc main.o uart.o gpio.o -o app
rm -f *.o app
```

## 4. 힌트

<details>
<summary>힌트 1 — 값이 호출 사이에 남으려면?</summary>

지역 변수의 **저장 기간**을 automatic → static 으로 바꾸면 된다. 키워드는 `static`. 초기값은 `= 0` 을 안 써도 0이다 (C11 6.7.9p10). 초기화는 매 호출이 아니라 **프로그램 시작 전 한 번**이다.

</details>

<details>
<summary>힌트 2 — init-once는 플래그 하나</summary>

`static bool ready;` 를 두고 `if (!ready) { ...만들기...; ready = true; }`. 테이블 자체도 static 이어야 return 후 주소가 유효하다 (지역 배열 주소를 돌려주면 clang이 `-Wreturn-stack-address` 로 잡는다).

</details>

<details>
<summary>힌트 3 — 재진입 가능하게 만들려면</summary>

함수 안에 쓰기 가능한 static 상태를 두지 않는다. 결과를 담을 버퍼를 **호출자가 넘긴다** (`strtok` → `strtok_r`, `asctime` → `asctime_r` 과 같은 해법). 읽기 전용 `static const` 표는 공유해도 안전하다.

</details>

<details>
<summary>힌트 4 — 링 버퍼 가득 참 판정</summary>

`head`, `tail` 을 계속 증가만 시키는 free-running 인덱스로 쓰면 `head - tail` 이 들어 있는 개수다 (unsigned 뺄셈은 wrap 되어도 정의됨). 가득 참 = `head - tail == TXQ_SIZE`, 빔 = `head == tail`. 칸 번호는 `head % TXQ_SIZE`.

</details>

<details>
<summary>힌트 5 — nm 읽는 법</summary>

**대문자 = 외부 연결(global), 소문자 = 내부(local)**. T/t 코드, D/d 초기값 있는 데이터, B/b 0 초기화 데이터, C common(tentative), U 다른 파일에 있음(미정의), S 그 밖의 섹션. 맥은 C 이름 앞에 `_` 가 붙는다.

</details>

## 5. 풀이

### 핵심 — 세 가지 속성을 분리해서 본다

| 선언 위치 · 형태 | 범위 (scope) | 연결 (linkage) | 저장 기간 | 초기값 없을 때 |
|---|---|---|---|---|
| 파일 스코프 `int g;` | 파일 끝까지 | external | static | 0 (tentative → 정의) |
| 파일 스코프 `static int s;` | 파일 끝까지 | **internal** | static | 0 |
| 파일 스코프 `extern int g;` | 파일 끝까지 | 앞 선언 따름, 없으면 external | (정의 아님) | — |
| 블록 `int a;` | 블록 | none | automatic | **불확정(indeterminate)** |
| 블록 `static int c;` | 블록 | none | **static** | 0, 시작 전 1회 |
| 블록 `extern int g;` | 블록 | 보이는 선언 따름, 없으면 external | (그 객체의 것) | — |
| `void f(void) {...}` | 파일 | external (기본) | — | — |
| `static void f(void) {...}` | 파일 | **internal** | — | — |

- C11 6.2.2p3: 파일 스코프 + `static` → internal linkage. p5: 아무 지정자 없는 파일 스코프 객체/함수 → external.
- C11 6.2.4p3: 외부/내부 연결이 있거나 `static` 으로 선언된 객체는 **static storage duration** — 프로그램 전체 수명, 시작 전 1회 초기화.
- 그래서 핵심 한 줄: **파일 스코프 변수는 원래 static 수명이다 → 거기서 `static` 은 연결을 바꾼다. 블록 안 변수는 원래 연결이 없다 → 거기서 `static` 은 수명을 바꾼다.**

### Part A — 함수 내 static: 연결은 없고 수명만 길다

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">seq_next() 를 3번 호출할 때: auto 지역 vs static 지역</text>
  <g fill="currentColor"><text x="10" y="70">auto seq</text><text x="10" y="88" font-size="12">(스택 프레임)</text>
  <text x="10" y="150">static seq</text><text x="10" y="168" font-size="12">(.bss, 이름 seq_next.seq)</text></g>
  <g stroke="currentColor" stroke-dasharray="4 3" fill="none">
    <rect x="190" y="50" width="120" height="40" class="f-warn"/>
    <rect x="370" y="50" width="120" height="40" class="f-warn"/>
    <rect x="550" y="50" width="120" height="40" class="f-warn"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="200" y="75">0 → 1 (소멸)</text><text x="380" y="75">0 → 1 (소멸)</text><text x="560" y="75">0 → 1 (소멸)</text>
  </g>
  <rect x="160" y="130" width="540" height="40" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="170" y="155">0 (시작 전)</text><text x="260" y="155">→ 1</text><text x="440" y="155">→ 2</text><text x="620" y="155">→ 3</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="250" y="112">호출 1</text><text x="430" y="112">호출 2</text><text x="610" y="112">호출 3</text>
  </g>
  <text x="10" y="200" fill="currentColor">auto 는 호출마다 프레임에 새로 생기고 return 하면 사라진다 → 항상 1.</text>
  <text x="10" y="222" fill="currentColor">static 은 startup 코드가 .bss 를 0으로 채울 때 한 번 생겨 리셋까지 산다. 이름은 함수 안에서만 보인다.</text>
</svg>
```

| 함수 | 시작 코드 | 정답 | 결과 |
|---|---|---|---|
| `seq_next` | `uint32_t seq = 0;` | `static uint32_t seq;` | 1, 2, 3 |
| `other_next` | `uint32_t seq = 100;` | `static uint32_t seq = 100;` | 101, 102 — `seq_next` 와 별개 |
| `first_time` | `bool called = false;` | `static bool called;` | true, false, false |

- `static uint32_t seq = 100;` 의 `= 100` 은 **대입이 아니라 초기화**다. 함수가 호출될 때마다 실행되는 문장이 아니다. 그래서 초기값은 **상수식(constant expression)** 이어야 한다 (C11 6.7.9p4). `static int x = read_hw();` 는 C에서 컴파일 에러:

```text
si.c:3:20: error: initializer element is not a compile-time constant
    3 |     static int x = read_hw();
```
  (C++ 은 이걸 허용하고 첫 호출 때 스레드 안전하게 초기화한다 — "magic statics". C 와 다르다.)
- 두 함수의 `seq` 는 **연결이 없으니(no linkage)** 서로 다른 객체다. 컴파일러는 심볼 이름을 `seq_next.seq`, `other_next.seq` 처럼 함수 이름으로 구분한다 (clang 방식. GCC는 보통 `seq.0`, `seq.1`).

### Part B — init-once 와 "static 버퍼를 돌려주는" 함수

```c
static const uint8_t *crc8_table(void) {
    static uint8_t table[256];
    static bool    ready;
    if (!ready) { /* 256칸 채우기 */ ready = true; g_table_builds++; }
    return table;            /* static 배열 → return 후에도 유효 */
}
```

1. **수명**: `table` 은 static 저장 기간이라 함수가 끝나도 살아 있다 (C11 6.2.4p2: 객체는 수명 동안 주소가 일정). 같은 코드를 auto 배열로 쓰면 clang이 바로 경고한다:
   ```text
   rs.c:2:62: warning: address of stack memory associated with local variable 't' returned [-Wreturn-stack-address]
   ```
2. **한 번만**: 두 번째 호출부터는 `ready == true` 라 테이블을 다시 만들지 않는다 → `g_table_builds == 1`.
3. **경쟁(race)**: `if (!ready) {...; ready = true;}` 는 check-then-set 이다. RTOS 태스크 둘이 동시에 처음 부르면 **둘 다 만든다**. CRC 표처럼 결과가 같으면 낭비로 끝나지만, 하드웨어 초기화라면 두 번 실행되는 버그다 → 임계 구역(인터럽트 금지/뮤텍스)이나 C11 `call_once` / 원자 플래그로 막는다.

`hex32()` 는 static `buf` 하나를 계속 돌려준다. 테스트가 보여주듯:

| 순서 | 호출 | `p1` 이 가리키는 문자열 |
|---|---|---|
| 1 | `p1 = hex32(0xDEADBEEF)` | `"0xDEADBEEF"` |
| 2 | `p2 = hex32(0x1234)` | `"0x00001234"` ← 덮어써짐, `p1 == p2` |

`printf("%s %s", hex32(a), hex32(b))` 는 같은 문자열을 두 번 찍는다. 메인 루프와 ISR이 같이 부르면 출력이 깨진다. 이런 함수를 **재진입 불가(non-reentrant)** 라고 한다 — libc의 `strtok`, `asctime`, `inet_ntoa` 가 대표. 해법은 상태를 호출자에게 넘기는 것: `hex32_r(v, out)`. 함수 안에 남겨도 되는 static은 `static const char digits[]` 같은 **읽기 전용 표**뿐이다.

### Part C — static 은 C의 `private`: 모듈 설계

```text
"uart.h" (공개)             "uart.c" (구현)
extern uint32_t uart_bytes_sent;   static volatile uint8_t  s_txq[8];   ← 내부 연결
bool uart_init(void);              static volatile uint32_t s_head, s_tail;
bool uart_putc(uint8_t);           static bool s_ready;  static uint32_t s_dropped;
void uart_tx_isr(void);            static void hw_write_tdr(uint8_t);
uint32_t uart_dropped(void);       uint32_t uart_bytes_sent;           ← 외부 연결 정의
```

- `uart_putc`: 미초기화 → `false`. `s_head - s_tail == 8` 이면 가득 → `s_dropped++`, `false`. 아니면 `s_txq[s_head % 8] = c` 를 **먼저** 쓰고 `s_head` 를 **나중에** 올린다 (ISR이 인덱스를 보고 읽으므로).
- `uart_tx_isr`: 비었으면 return. 아니면 `s_txq[s_tail % 8]` 을 TDR로, `s_tail++`, `uart_bytes_sent++`.
- main(생산자)만 `s_head` 를, ISR(소비자)만 `s_tail` 을 쓰는 **단일 생산자·단일 소비자** 구조라 락 없이 동작한다. `volatile` 은 컴파일러가 접근을 없애거나 서로 순서를 바꾸지 못하게 한다 (데이터 배열도 volatile이어야 "데이터 → 인덱스" 순서가 컴파일러 수준에서 지켜진다). 싱글 코어 Cortex-M에서는 이걸로 충분하고, 멀티코어/DMA면 배리어(`__DMB`)나 C11 atomics가 필요하다 (C-B09 참고).
- 다른 파일은 `s_dropped` 를 직접 못 고친다 — 접근 함수 `uart_dropped()` 로만 본다. `uart_bytes_sent` 처럼 전역을 노출하는 건 편하지만 아무 데서나 쓸 수 있으니, 실무에선 getter를 선호한다 (MISRA C:2012 Rule 8.7: 한 파일에서만 쓰는 건 외부 연결로 두지 말 것, Rule 8.8: 내부 연결이면 모든 선언에 `static`).

#### 실험 1 — `nm` 으로 심볼 보기 (맥, Mach-O, `-O0`)

```text
$ cc -std=c11 -Wall -Wextra -Werror -O0 -c main.c uart.c gpio.c
$ nm uart.o
00000000000000b8 t _hw_putc
0000000000000064 t _hw_reset
00000000000000ec d _s_baud
0000000000000195 b _s_rxbuf
00000000000000dc T _uart_baud
0000000000000000 T _uart_init
0000000000000190 b _uart_init.calls
0000000000000194 b _uart_init.done
00000000000000e8 D _uart_log_level
0000000000000070 T _uart_send
0000000000000004 C _uart_tx_bytes
0000000000000000 t ltmp0
00000000000000e8 d ltmp1
0000000000000190 b ltmp2
00000000000000f0 s ltmp3
$ nm main.o | grep ' U '
                 U _gpio_init
                 U _gpio_state
                 U _gpio_write
                 U _printf
                 U _uart_baud
                 U _uart_init
                 U _uart_log_level
                 U _uart_send
                 U _uart_tx_bytes
$ cc main.o uart.o gpio.o -o app && ./app
init calls=1,2 baud=9600
uart_tx_bytes=5 log_level=2 gpio=0x20
$ nm app | grep -E "hw_reset|uart_tx_bytes|calls"
00000001000006e0 t _hw_reset
0000000100000778 t _hw_reset
0000000100008008 b _uart_init.calls
0000000100008054 S _uart_tx_bytes
```

`nm -m uart.o` 로 보면 글자 대신 섹션과 external/non-external 이 직접 나온다 (일부):

```text
0000000000000064 (__TEXT,__text) non-external _hw_reset
00000000000000ec (__DATA,__data) non-external _s_baud
0000000000000195 (__DATA,__bss) non-external _s_rxbuf
00000000000000e8 (__DATA,__data) external _uart_log_level
0000000000000004 (common) (alignment 2^2) external _uart_tx_bytes
```

읽는 법:

| 소스 (proj/uart.c) | Mach-O `nm` | ELF `nm` (arm-none-eabi) | Mach-O 섹션 → ELF 섹션 |
|---|---|---|---|
| `unsigned uart_init(...) {}` | `T _uart_init` | `T uart_init` | `__TEXT,__text` → `.text` |
| `static void hw_reset(void) {}` | `t _hw_reset` | `t hw_reset` | 〃 |
| `int uart_log_level = 2;` | `D` | `D` | `__DATA,__data` → `.data` |
| `static uint32_t s_baud = 115200;` | `d` | `d` | 〃 |
| `static uint8_t s_rxbuf[64];` | `b` | `b` | `__DATA,__bss` → `.bss` |
| 함수 내 `static unsigned calls;` | `b _uart_init.calls` | `b uart_init.calls` | `.bss` |
| `uint32_t uart_tx_bytes;` (tentative) | `C` (.o) → `S` (링크 후) | `B` (기본 `-fno-common`) | `__DATA,__common` → `.bss` |
| 다른 파일의 것을 사용 | `U` | `U` | — |

- 맥(Mach-O)은 C 심볼 앞에 `_` 를 붙인다. ELF(리눅스·arm-none-eabi)는 안 붙인다. 그래서 맥에서 어셈블리로 C 함수를 부를 땐 `bl _foo`.
- `ltmpN`, `l_.str` 은 어셈블러가 만든 지역 임시 라벨 / 문자열 리터럴이다. 무시해도 된다. `s` / `S` 는 "text·data·bss 가 아닌 다른 섹션"(`__compact_unwind`, `__common`, `__const` 등).
- 링크된 `app` 에도 `t _hw_reset` 이 **두 개** 있다. 각자 자기 파일에서만 불린다.

ELF는 맥 clang으로도 직접 볼 수 있다 (링크 없이 오브젝트만):

```text
$ cc --target=armv7em-none-eabi -mthumb -ffreestanding -std=c11 -Wall -Wextra -Werror -O0 -c uart.c gpio.c
$ nm -A uart.o gpio.o
uart.o: 00000084 t hw_putc
uart.o: 00000050 t hw_reset
uart.o: 00000004 d s_baud
uart.o: 0000000c b s_rxbuf
uart.o: 0000009c T uart_baud
uart.o: 00000000 T uart_init
uart.o: 00000000 b uart_init.calls
uart.o: 00000004 b uart_init.done
uart.o: 00000000 D uart_log_level
uart.o: 00000060 T uart_send
uart.o: 00000008 B uart_tx_bytes
gpio.o: 00000000 T gpio_init
gpio.o: 00000058 T gpio_state
gpio.o: 00000018 T gpio_write
gpio.o: 00000008 t hw_reset
gpio.o: 00000000 b s_odr
```

`-O2` 로 다시 빌드하면 `uart.o` 에서 `t _hw_putc`, `t _hw_reset`, `b _s_rxbuf` 가 **사라진다** (실제 `nm uart_O2.o` 결과에 없음). static 함수는 다른 파일이 부를 수 없으니 컴파일러가 인라인하고 원본을 지울 수 있고, 쓰기만 하고 읽지 않는 static 배열도 지운다. 외부 연결 함수(`T`)는 누가 부를지 모르니 못 지운다 — 펌웨어에서 `-ffunction-sections -fdata-sections -Wl,--gc-sections` 나 LTO를 쓰는 이유다.

#### 실험 2 — 이름 충돌 재현과 해결

`-DDEMO_COLLISION` 은 `uart.c`, `gpio.c` 둘 다에서 `hw_reset` 의 `static` 을 뺀다.

```text
$ cc -std=c11 -Wall -Wextra -Werror -O0 -DDEMO_COLLISION -c uart.c gpio.c
$ nm -A uart.o gpio.o | grep hw_reset
uart.o: 0000000000000000 T _hw_reset
gpio.o: 0000000000000000 T _hw_reset
$ cc main.o uart.o gpio.o -o app
duplicate symbol '_hw_reset' in:
    proj/gpio.o
    proj/uart.o
ld: 1 duplicate symbols
clang: error: linker command failed with exit code 1 (use -v to see invocation)
```

(ld는 절대 경로를 찍는다. 여기선 `proj/` 로 줄였다.) 컴파일은 각 파일 따로라 성공하고, **링크에서야** 터진다. arm-none-eabi-gcc(GNU ld)라면 `multiple definition of ... ; ... first defined here` 형태로 나온다. 해결은 파일 안에서만 쓰는 함수에 `static` 을 붙이는 것 — 기본 빌드처럼 둘 다 `t` 가 되어 공존한다. 이름을 `uart_hw_reset` 처럼 모듈 접두사로 바꾸는 것도 실무 규칙이지만, 1순위는 **안 보여도 되는 건 static**.

#### 실험 3 — tentative definition 과 `-fcommon`

```svg
<svg viewBox="0 0 740 220" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">int debug_flags; 가 uart.c 와 gpio.c 에 모두 있을 때</text>
  <rect x="10" y="40" width="200" height="50" class="f-acc" stroke="currentColor"/>
  <text x="20" y="62" fill="currentColor" font-family="monospace">int debug_flags;</text>
  <text x="20" y="80" fill="currentColor" font-size="12">초기값 없음 = tentative</text>
  <rect x="270" y="34" width="220" height="50" class="f-ok" stroke="currentColor"/>
  <text x="280" y="55" fill="currentColor">-fcommon → 심볼 C (common)</text>
  <text x="280" y="74" fill="currentColor" font-size="12">맥 Apple clang 기본값 (실측)</text>
  <rect x="270" y="104" width="220" height="50" class="f-warn" stroke="currentColor"/>
  <text x="280" y="125" fill="currentColor">-fno-common → S / B (정의)</text>
  <text x="280" y="144" fill="currentColor" font-size="12">GCC 10+, LLVM clang 11+ 기본값</text>
  <rect x="540" y="34" width="190" height="50" class="f-ok" stroke="currentColor"/>
  <text x="550" y="55" fill="currentColor">링커가 하나로 합침</text>
  <text x="550" y="74" fill="currentColor" font-size="12">조용히 성공 (가장 큰 크기)</text>
  <rect x="540" y="104" width="190" height="50" class="f-warn" stroke="currentColor"/>
  <text x="550" y="125" fill="currentColor">duplicate symbol</text>
  <text x="550" y="144" fill="currentColor" font-size="12">링크 실패</text>
  <g stroke="currentColor" fill="none">
    <path d="M210 60 L270 59"/><path d="M210 70 L270 129"/>
    <path d="M490 59 L540 59"/><path d="M490 129 L540 129"/>
  </g>
  <text x="10" y="190" fill="currentColor"><tspan font-family="monospace">int debug_flags = 0;</tspan> 처럼 초기값을 쓰면 tentative 가 아니라 정의 → -fcommon 이어도 항상 충돌.</text>
  <text x="10" y="212" fill="currentColor">표준상으로는 외부 정의가 둘이면 UB (C11 6.9p5). common 은 오래된 확장(J.5.11)일 뿐.</text>
</svg>
```

C11 6.9.2p2: 파일 스코프에서 초기값 없이(그리고 `extern` 없이) 선언한 객체는 **tentative definition**. 그 파일에 진짜 정의가 없으면 파일 끝에서 `= 0` 정의가 된다. 한 파일 안에선 `int x; int x;` 를 여러 번 써도 된다. 문제는 **두 파일**에 있을 때다.

```text
$ cc -### -std=c11 -c uart.c 2>&1 | tr ' ' '\n' | grep common
"-fcommon"                                  ← Apple clang 드라이버가 맥에서 넣는 기본값
# ① 플래그 없음 (= Apple clang 기본 -fcommon)
$ cc -std=c11 -Wall -Wextra -Werror -O0 -DDEMO_TENTATIVE -c uart.c gpio.c
$ nm -A uart.o gpio.o | grep debug_flags
uart.o: 0000000000000004 C _debug_flags
gpio.o: 0000000000000004 C _debug_flags
$ cc main.o uart.o gpio.o -o app && nm app | grep debug_flags
0000000100008054 S _debug_flags            ← 합쳐져서 하나, 에러 없음
# ② 명시적 -fcommon — ①과 똑같다 (기본값이 -fcommon 이라는 확인)
$ cc -std=c11 -Wall -Wextra -Werror -O0 -fcommon -DDEMO_TENTATIVE -c uart.c gpio.c
$ nm -A uart.o gpio.o | grep debug_flags
uart.o: 0000000000000004 C _debug_flags
gpio.o: 0000000000000004 C _debug_flags
$ cc main.o uart.o gpio.o -o app && echo "link OK" && nm app | grep debug_flags
link OK
0000000100008054 S _debug_flags
# ③ 명시적 -fno-common (GCC 10+ / LLVM clang 11+ / arm-none-eabi-gcc 10+ 의 기본 동작)
$ cc -std=c11 -Wall -Wextra -Werror -O0 -fno-common -DDEMO_TENTATIVE -c uart.c gpio.c
$ nm -A uart.o gpio.o | grep debug_flags
uart.o: 00000000000001dc S _debug_flags
gpio.o: 0000000000000114 S _debug_flags
$ cc main.o uart.o gpio.o -o app
duplicate symbol '_debug_flags' in:
    proj/gpio.o
    proj/uart.o
ld: 1 duplicate symbols
```

같은 Apple clang으로 ELF(Cortex-M) 타깃을 빌드하면 기본값이 바뀐다:

```text
$ cc --target=armv7em-none-eabi -mthumb -ffreestanding -std=c11 -Wall -Wextra -Werror -O0 -DDEMO_TENTATIVE -c uart.c
$ nm uart.o | grep -E "debug|tx_bytes"
0000000c B debug_flags                     ← ELF 타깃 기본 = -fno-common → 진짜 .bss 정의
00000008 B uart_tx_bytes
$ cc --target=armv7em-none-eabi ... -fcommon -DDEMO_TENTATIVE -c uart.c
$ nm uart.o | grep -E "debug|tx_bytes"
00000004 C debug_flags
00000004 C uart_tx_bytes
```

주의: **LLVM clang 11 · GCC 10 부터 기본은 `-fno-common`** 이지만, 이 맥의 Apple clang 21은 Darwin 타깃에 `-fcommon` 을 넣는다 (위 `-###` 실측, ①=② 결과 동일). 같은 컴파일러도 ELF 타깃에선 `-fno-common` 이 기본이다 (위 실측). 즉 **"맥에선 링크되던 코드가 arm-none-eabi-gcc 10+ 로 옮기자 duplicate symbol"** 이 실제로 생긴다. 옛 펌웨어 코드를 새 툴체인으로 올릴 때 가장 흔한 링크 에러다.

해결은 플래그가 아니라 코드: 헤더에 `extern int debug_flags;` (선언), `.c` 한 곳에 `int debug_flags;` (정의). 정말 파일마다 따로면 `static`.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 32/32 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o solution_san && ./solution_san   # 32/32, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 헤더에 `static int counter;` | include 한 `.c` 마다 **별도 사본**. 링크 에러도 없이 값이 갈라진다 (`demo_header_static/` 실측: `a.c counter=2, main.c counter=0`, `nm` 에 `b _counter` 두 개) | 헤더 `extern int counter;` + `.c` 한 곳에서 정의, 또는 getter 함수 |
| 헤더에 `int g;` | include 한 파일마다 tentative 정의 → `-fno-common` 툴체인에서 duplicate symbol | 헤더엔 `extern int g;` |
| 파일 내부 helper에 `static` 빠뜨림 | 외부 연결이 되어 다른 파일의 같은 이름과 충돌 (실험 2), 최적화로 지울 수도 없음 | `static void hw_reset(void)` |
| `-fcommon` 에 기대기 | 타입이 달라도 조용히 합친다. `demo_common/`: `a.c` 의 `int buf;` 와 `b.c` 의 `char buf[100];` 가 링크 성공, 크기 100으로 합쳐지고 `a_touch()` 가 `buf[0]` 을 `0x44` 로 덮어씀 (C11 6.2.7p2 위반 = UB) | `-fno-common` 로 빌드해 링크 에러로 잡기 |
| `static int x = read_hw();` | static 객체 초기값은 상수식이어야 함 → 컴파일 에러 | `static int x; static bool done; if (!done) { x = read_hw(); done = true; }` |
| 지역 배열 주소 반환 | 프레임이 사라진 뒤 dangling (clang `-Wreturn-stack-address`) | `static` 배열 또는 호출자 버퍼 |
| `printf("%s %s", hex32(a), hex32(b))` | 같은 static 버퍼 → 두 번째 값이 두 번 찍힘 | `hex32_r(a, ba), hex32_r(b, bb)` |
| 함수 내 static 상태를 main과 ISR이 같이 사용 | 재진입 불가: ISR이 중간에 끼면 상태가 깨진다 | 상태를 컨텍스트 구조체로 빼거나, 임계 구역 |
| `extern int x = 5;` | `extern` 이어도 초기값이 있으면 **정의**다 (clang `-Wextern-initializer`, `nm`: `D _x`) | 헤더엔 `extern int x;`, `.c` 에 `int x = 5;` |

`demo_header_static/` · `demo_common/` 재현 명령:

```bash
cd demo_header_static && cc -std=c11 -Wall -Wextra -Werror -c a.c main.c && cc a.o main.o -o hdr && ./hdr && nm -A a.o main.o | grep counter
# a.c counter=2, main.c counter=0
# a.o: 0000000000000060 b _counter
# main.o: 00000000000000a0 b _counter
cd ../demo_common && cc -std=c11 -Wall -Wextra -Werror -c a.c b.c && cc a.o b.o -o common_demo && ./common_demo
# buf[0]=0x44 sizeof(buf) here=100        (nm a.o: C _buf 크기 4, nm b.o: C _buf 크기 0x64)
# size -m common_demo → Section __common: 100 (zerofill)
# -fno-common 으로 다시 빌드하면: duplicate symbol '_buf' in: b.o a.o
```

## 8. 면접 Q&A

<details>
<summary>Q1. C에서 static 의 의미를 모두 말해보세요.</summary>

① 파일 스코프 변수·함수: **internal linkage** — 그 번역 단위(translation unit) 밖에서 안 보임. ② 블록 스코프 변수: **static storage duration** — 프로그램 전체 수명, 시작 전 1회 초기화, 연결은 여전히 없음. ③ C99 배열 매개변수 `void f(int a[static 4])`: "최소 4개 원소가 있는 유효한 포인터를 넘긴다"는 약속 (C11 6.7.6.3p7) — 컴파일러 최적화 힌트, NULL 금지. 꼬리질문 "C++ 은?" → 클래스 static 멤버(인스턴스와 무관한 하나) 의미가 추가된다.

</details>

<details>
<summary>Q2. scope, linkage, storage duration 의 차이는?</summary>

scope = 소스에서 그 이름을 쓸 수 있는 영역(컴파일 타임, 한 파일 안). linkage = 다른 선언(다른 파일 포함)이 **같은 객체**를 가리키느냐 (external/internal/none). storage duration = 객체가 메모리에 살아 있는 기간 (static/automatic/allocated/thread). 함수 내 static 은 scope 블록, linkage none, duration static — 세 개가 다 다르다는 게 핵심 예.

</details>

<details>
<summary>Q3. 헤더에 전역 변수를 넣으려면 어떻게 하나요? int g; 를 그냥 넣으면?</summary>

헤더엔 `extern int g;` (선언, 메모리 없음), `g.c` 한 곳에 `int g;` 또는 `int g = 0;` (정의). `int g;` 를 헤더에 넣으면 include 한 파일마다 tentative 정의가 생겨 `-fno-common` (GCC 10+/LLVM clang 11+ 기본)에서 `multiple definition`/`duplicate symbol`. `-fcommon` 이면 링크는 되지만 타입이 달라도 조용히 합쳐지는 위험. `static int g;` 를 넣으면 파일마다 사본이 생겨 값이 공유되지 않는다.

</details>

<details>
<summary>Q4. 함수 내 static 변수는 언제 초기화되나요? 스레드·ISR 에 안전한가요?</summary>

C에서는 초기값이 상수식이라 **실행 전에** 이미 정해진다: 0이면 `.bss` (Cortex-M Reset_Handler가 0으로 채움), 0이 아니면 `.data` (플래시의 초기값 이미지를 RAM으로 복사). 그래서 "초기화 자체"의 경쟁은 없다 (C++ 의 첫 호출 초기화와 다름). 하지만 그 뒤 값을 읽고 쓰는 건 일반 공유 변수와 같아서, `static bool done; if(!done){...; done=true;}` 같은 check-then-set 이나 `count++` 는 ISR/태스크와 경쟁한다 → 인터럽트 금지 구간, 뮤텍스, C11 atomics. (C-B11 메모리 배치 참고)

</details>

<details>
<summary>Q5. duplicate symbol / undefined symbol 링크 에러를 어떻게 추적하나요?</summary>

`nm -A *.o | grep sym` 으로 누가 `T/D/B/C`(정의)이고 누가 `U`(사용)인지 본다. 정의가 둘 → 하나를 `static` 으로 하거나 헤더의 정의를 `extern` 선언으로. 정의가 0 → 그 `.c` 를 링크 안 했거나, 정의에 `static` 이 붙어 있거나(소문자로 보임), C++ 에서 부르는데 `extern "C"` 가 빠져 이름이 맹글링됨. 맥은 `_` 접두사, ELF는 없음을 기억.

</details>

<details>
<summary>Q6. static 함수가 코드 크기와 성능에 주는 이점은?</summary>

다른 파일에서 못 부르니 컴파일러가 모든 호출처를 안다 → 인라인 후 원본 삭제, 호출 규약 변경, 상수 전파가 자유롭다. 실험 1에서 `-O2` 로 빌드하자 `t _hw_putc`, `t _hw_reset` 이 오브젝트에서 사라졌다. 외부 함수는 다른 파일이 부를 수 있어 남는다 → `--gc-sections` 나 LTO 가 필요. 플래시가 작은 MCU에선 이 차이가 크다.

</details>

<details>
<summary>Q7. 함수 선언에 extern 을 붙이는 것과 안 붙이는 것의 차이는? 블록 안 extern 은?</summary>

함수 선언은 기본이 extern 이라 차이 없다 (C11 6.2.2p5). 블록 안 `extern int q;` 는 새 변수를 만드는 게 아니라 **이미 보이는 q의 연결을 따른다** — 위에 `static int q;` 가 있으면 내부 연결 q 를 가리킨다 (6.2.2p4, 추가 연습 5). 반대로 `extern int y;` 다음 `static int y;` 는 같은 파일에서 외부·내부가 섞여 UB (6.2.2p7); clang은 에러로 막는다: `error: static declaration of 'y' follows non-static declaration`.

</details>

<details>
<summary>Q8. 꼬리질문 — 여러 인스턴스가 필요한 드라이버(UART1, UART2)는 static 상태로 어떻게 만드나요?</summary>

함수 내/파일 static 은 **인스턴스가 하나**(싱글톤)다. 인스턴스가 여럿이면 상태를 `struct uart_ctx` 로 묶고 모든 API가 `ctx *` 를 받게 한다 (추가 연습 2의 디바운스). 메모리는 `static struct uart_ctx s_uart[2];` 처럼 정적 배열로 잡아 malloc 없이 쓴다 (추가 연습 4). HAL의 `UART_HandleTypeDef *huart` 가 이 패턴이다.

</details>

## 9. 추가 연습

### 연습 1 — static 메모이제이션

`fib(n)` (n ≤ 93) 을 함수 내 `static uint64_t memo[94]` 로 캐시하라. 두 번째 `fib(50)` 은 추가 계산이 0이어야 한다.

<details>
<summary>답 보기</summary>

`if (memo[n]) return memo[n]; memo[n] = fib(n-1) + fib(n-2);` — static 배열은 모든 호출(재귀 포함)이 **공유**한다. `fib(2)..fib(50)` 각 1번씩, 계산 49회. 이후 같은 호출은 0회, `fib(93)` 은 43회만 더. 반대로 말하면 이 함수는 재진입 불가이고 ISR에서 부르면 안 된다.

</details>

### 연습 2 — 싱글톤 디바운서의 한계

버튼 디바운스(같은 값 3번 연속이면 확정)를 함수 내 static 으로 만든 `debounce1(raw)` 에 버튼 A(계속 1), B(계속 0)를 번갈아 넣으면 어떻게 되나? 컨텍스트 버전 `debounce(deb_t *d, raw)` 로 고쳐라.

<details>
<summary>답 보기</summary>

`debounce1` 의 `last/cnt` 를 A와 B가 번갈아 덮어써서 `cnt` 가 계속 1로 리셋 → A가 영원히 확정되지 않는다. 상태를 `deb_t` 로 빼서 버튼마다 하나씩 두면 A=1, B=0 으로 정상 확정. "함수 내 static = 인스턴스 하나" 를 기억.

</details>

### 연습 3 — WARN_ONCE 매크로

로그 폭주를 막는 `WARN_ONCE(cond)` 를 만들라. **호출 위치마다** 한 번씩만 찍혀야 한다.

<details>
<summary>답 보기</summary>

`do { static bool warned_; if ((cond) && !warned_) { warned_ = true; ... } } while (0)` — 매크로가 펼쳐진 블록마다 **별개의** static 변수가 생긴다. 두 자리에서 5번씩 불러도 총 2번. Linux 커널 `WARN_ONCE`/`printk_once` 가 같은 원리 (커널은 이 플래그들을 전용 데이터 섹션에 모아 둔다).

</details>

### 연습 4 — malloc 없는 정적 풀

`soft_timer` 4개짜리 정적 풀에서 `timer_alloc()` / `timer_free()` 를 구현하라. 5번째는 NULL, 반납한 칸은 다시 할당돼야 한다.

<details>
<summary>답 보기</summary>

`static soft_timer s_pool[4]; static bool s_used[4];` — 파일 static 이라 다른 모듈은 풀을 직접 못 건드리고 API로만 쓴다. 메모리 사용량이 링크 시점에 `.bss` 크기로 확정되므로 힙 단편화가 없다 (MISRA/안전 인증 펌웨어가 동적 할당을 금지하는 이유). `timer_free` 는 `t - s_pool` 로 인덱스를 구한다.

</details>

### 연습 5 — 블록 안 extern 은 무엇을 가리키나

`static int q = 5;` 아래 함수 안에서 `extern int q;` 로 읽으면 무엇이 나오나? 링크 에러가 나나?

<details>
<summary>답 보기</summary>

C11 6.2.2p4: `extern` 선언은 **이미 보이는 선언의 연결을 따른다**. 위의 `q` 가 internal 이므로 블록 안 `q` 도 같은 internal 객체 → 5, 그리고 `q = 42` 후엔 42. 외부 심볼을 찾지 않으니 링크 에러도 없다. 순서를 뒤집어 `extern int y; static int y;` 로 쓰면 6.2.2p7 의 UB 이고 clang은 에러를 낸다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 12/12 passed
```

## 10. 핵심 정리

- 이름의 세 속성: **scope**(어디서 쓰나) · **linkage**(다른 선언과 같은 객체인가) · **storage duration**(언제까지 사나).
- 파일 스코프 `static` = **internal linkage** (C의 private). 블록 `static` = **static 수명**, 연결은 none.
- static 객체는 시작 전 1회 초기화, 초기값은 상수식, 생략하면 0 (`.bss`), 0이 아니면 `.data`.
- `nm`: 대문자 외부 / 소문자 내부 — `T t D d B b C U S`. 맥은 `_` 접두사, ELF는 없음.
- 헤더엔 `extern` 선언, `.c` 한 곳에 정의. 파일 안에서만 쓰는 건 전부 `static`.
- tentative `int x;` 두 파일 → `-fno-common`(GCC 10+/LLVM 11+, arm-none-eabi 기본) 에선 duplicate symbol, `-fcommon`(맥 Apple clang 기본 실측) 에선 조용히 합쳐짐.
- 함수 내 static 은 init-once·캐시·카운터에 편하지만 **재진입 불가 · 인스턴스 하나 · ISR 경쟁**.
- static 함수는 컴파일러가 인라인·삭제할 수 있어 코드가 작아진다.

## 참고자료

- C11 표준 (N1570) 6.2.1 Scopes, 6.2.2 Linkages, 6.2.4 Storage durations, 6.7.9 Initialization, 6.9 External definitions, 6.9.2 Tentative definitions, J.5.11 Multiple external definitions — [N1570 PDF](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf)
- [GCC 10 Porting Guide — Default to -fno-common](https://gcc.gnu.org/gcc-10/porting_to.html)
- [Clang 11 Release Notes — -fno-common 기본값](https://releases.llvm.org/11.0.0/tools/clang/docs/ReleaseNotes.html)
- [GNU Binutils — nm 심볼 타입 문자](https://sourceware.org/binutils/docs/binutils/nm.html)
- `man nm` (macOS) — Mach-O 심볼 타입과 `-m` 출력
- MISRA C:2012 Rule 8.7, 8.8, 8.9 — 연결·static 사용 규칙
- Michael Barr, *Embedded C Coding Standard* — 모듈 static 변수·전역 변수 규칙
