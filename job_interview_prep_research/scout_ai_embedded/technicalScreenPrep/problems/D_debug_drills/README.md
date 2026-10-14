# D · Debug Drills

> **ID**: D1–D8 · **Priority**: P0 · **Pattern**: find the bug in 1 minute · **Time**: 15 min
> 파일: `buggy.c`(버그 8개 + 테스트) · `fixed.c`(정답) · 실행: `./run.sh D buggy` / `./run.sh D fixed` / 내 수정본 `./run.sh D my`

## 1. Problem

The interviewer pastes a short firmware function and asks: **"What is wrong with this code?"** You get about one minute per snippet.

For each of the eight drills below:

1. Read the **intended behavior**.
2. Name the bug in one sentence, and say **which input** triggers it.
3. Write the fixed line.
4. Say how you would have caught it (compiler warning, sanitizer, a unit test, a scope trace).

All eight functions live in `buggy.c`, together with a test `main()` that exposes every bug without crashing or hanging. Your job is to make it print `ALL PASS`.

| Drill | Function | Topic |
|---|---|---|
| D1 | `avg()` | average of `int16_t` samples |
| D2 | `timed_out()` | timeout on a 32-bit tick counter |
| D3 | `push()` | ring buffer push |
| D4 | `process_reverse()` | loop from the last element to the first |
| D5 | `decode_temp12()` | 12-bit two's-complement sensor register |
| D6 | `task()` + `TEMP_IRQHandler()` | counter shared with an ISR |
| D7 | `clear()` | zero a struct |
| D8 | `deb_update()` | debounced overheat detection |

### Constraints

- Change only the function bodies (and, for D6, the declaration of the shared variable).
- Keep the lines marked `TEST GUARD` and the simulation helpers (`irq_window`, `irq_disable`, `irq_enable`) as they are; they only exist so the test cannot hang or write out of bounds.
- The fixed file must pass with `-Wall -Wextra -Werror -fsanitize=address,undefined`.

### Clarifying questions to ask

1. What are the types and ranges of the inputs? (most of these bugs are about widths and signedness)
2. Can this run in interrupt context, or be interrupted?
3. Is the counter / timestamp free-running, i.e. does it wrap?
4. What should happen on the empty case (`n == 0`, empty buffer)?

## 2. Your workspace

아래 `buggy.c`가 작업 파일입니다. **복사본을 만들어 그 안에서 바로 고치세요.**

```
cd technicalScreenPrep
./run.sh D buggy                                   # 버그 8개가 모두 FAIL로 보임
cp problems/D_debug_drills/buggy.c problems/D_debug_drills/my.c
# my.c를 고친 뒤
./run.sh D my                                      # 목표: 8줄 모두 ok + ALL PASS
```

드릴마다 `D1 avg   ok` / `<-- BUG` 한 줄이 찍히므로 어느 것이 남았는지 바로 보입니다. 컴파일 경고(D4, D7)도 읽어 보세요. 그것 자체가 힌트입니다.

<div class="code" data-src="buggy.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 어디부터 의심하나</summary>

임베디드 C 버그는 대부분 네 종류입니다. **(1) 정수 폭/부호**(overflow, unsigned, sign extension), **(2) 경계**(0개, 꽉 참, 마지막 인덱스, 랩어라운드), **(3) 동시성**(ISR과 공유하는 변수), **(4) C 문법 함정**(`sizeof` 포인터). 스니펫을 보면 먼저 "타입이 뭐지?"를 묻고, 다음에 "0이면? 최댓값이면?"을 넣어 보세요.

</details>

<details>
<summary>Hint 2 — 숫자를 하나 넣어 보기</summary>

- D1: 8500을 8번 더하면? `int16_t` 최댓값은 32767입니다.
- D2: `start = 0xFFFFFF00`, `timeout = 512`이면 `start + timeout`은?
- D4: `n = 0`이면 `n - 1`은? `size_t`가 음수가 될 수 있나?
- D5: −25 °C의 12-bit 값 `0xE70`을 `int16_t`에 그대로 넣으면?

</details>

<details>
<summary>Hint 3 — 도구가 알려 주는 것</summary>

`./run.sh`는 ASan/UBSan을 켭니다. D1은 UBSan이 `division by zero`를, D7은 clang이 `-Wsizeof-pointer-memaccess`를 바로 보여 줍니다. D4는 `-Wtautological-unsigned-zero-compare`(clang) / `-Wtype-limits`(gcc, `-Wextra`에 포함)가 잡습니다. 도구가 못 잡는 것은 D2, D3(인덱스 논리), D5, D6, D8입니다. 이건 **테스트 입력**으로만 드러납니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어

각 드릴에서 말할 순서는 항상 같습니다: **① 버그 한 문장 → ② 터지는 입력 → ③ 고친 한 줄 → ④ 어떻게 찾았을지.** 30초 안에 ①②를 말하면 면접관은 이미 만족합니다. 아래는 드릴별 문제(English)와 답(펼치기)입니다.

### D1 · Average

**Intended behavior:** return the integer mean of `x[0..n-1]`. Inputs can be large (8500 = 85.00 °C in 0.01 °C units). If `n == 0`, return `AVG_INVALID` instead of dividing.

```c
int16_t avg(const int16_t *x, uint8_t n) {
    int16_t sum = 0;
    for (uint8_t i = 0; i < n; i++) sum += x[i];
    return sum / n;
}
```

<details>
<summary>💡 D1 답 펼치기</summary>

**버그 2개**
1. 합계가 `int16_t`라 넘칩니다. 8500 × 8 = 68000 > 32767 → 68000 − 65536 = **2464**가 되고, 평균은 8500이 아니라 **308**이 나옵니다.
2. `n == 0`이면 0으로 나누기 → UB. Cortex-M에서는 `DIV_0_TRP`가 켜져 있으면 UsageFault, 꺼져 있으면 조용히 0이 나옵니다.

**고친 코드**
```c
if (n == 0) return AVG_INVALID;
int32_t sum = 0;                       /* 255 * 32767 < 2^31 */
...
return (int16_t)(sum / n);
```

**어떻게 찾나**
- UBSan 출력: `buggy.c:42:16: runtime error: division by zero`
- 오버플로는 UBSan이 못 잡습니다. `sum += x[i]`는 `int`로 계산한 뒤 `int16_t`로 **변환**하는 것이라 UB가 아니라 implementation-defined이기 때문입니다. → "최대 입력 × 최대 개수" 테스트가 필요합니다. (`-fsanitize=implicit-conversion`을 켜면 clang이 잡아 줍니다.)
- 설명할 때: "누산기는 입력보다 넓게 — `n_max × max_value`가 들어가는 폭"이라고 말합니다.

</details>

### D2 · Timeout on a free-running tick counter

**Intended behavior:** return `true` once at least `timeout` ticks have elapsed since `start`. The counter wraps from `0xFFFFFFFF` to `0`, and that must not matter.

```c
bool timed_out(uint32_t now, uint32_t start, uint32_t timeout) {
    return now > start + timeout;
}
```

<details>
<summary>💡 D2 답 펼치기</summary>

**버그**: `start + timeout`이 랩어라운드하면 비교가 깨집니다. `start = 0xFFFFFF00`, `timeout = 512` → `start + timeout = 0x100`. 그러면 16 tick밖에 안 지난 `now = 0xFFFFFF10`도 `0x100`보다 크므로 **즉시 타임아웃**(반대로 deadline은 랩되지 않았는데 `now`가 먼저 0으로 넘어가면, 다음 한 바퀴(1 ms tick이면 약 49.7일) 동안 계속 false). 덤으로 `>`라서 정확히 `timeout`만큼 지난 순간을 놓칩니다(`timed_out(600, 100, 500)`이 false).

<div class="chart" data-src="chart1.svg" data-caption="D2: 덧셈 비교는 deadline이 0x100으로 넘어가 빨간 구간 전체가 '타임아웃'이 됨. 뺄셈 (now − start)은 랩을 넘어서도 경과 tick 수(16)를 정확히 줌"></div>

**고친 한 줄**
```c
return (uint32_t)(now - start) >= timeout;
```
부호 없는 뺄셈은 mod 2^32로 정의되어 있어서, 경과 시간이 2^32보다 작기만 하면 랩어라운드와 무관하게 정확합니다. `(uint32_t)` 캐스트는 `uint16_t` 카운터처럼 int로 승격되는 타입에서 꼭 필요하므로 습관으로 붙입니다.

**어떻게 찾나**: 컴파일러·sanitizer 모두 조용합니다(unsigned 랩은 정의된 동작). **랩 직전 `start`를 넣는 테스트**가 유일한 방법입니다. 실기에서는 49.7일(1 ms tick) 뒤에 터지는 "장기 구동 버그"로 나타납니다. 테스트 때는 tick 카운터 초기값을 `0xFFFF0000` 근처로 두면 몇 초 만에 재현됩니다.

</details>

### D3 · Ring buffer push

**Intended behavior:** store `v` at `head` and advance `head`, wrapping to 0 after `RB_N - 1`. Return `false` (and store nothing) when the buffer is full. One slot is kept empty, so capacity is `RB_N - 1`.

```c
#define RB_N 8
static int rb_buf[RB_N];
static unsigned rb_head, rb_tail;
bool push(int v) {
    if ((rb_head + 1) % RB_N == rb_tail) return false;
    rb_buf[rb_head++] = v;
    return true;
}
```

<details>
<summary>💡 D3 답 펼치기</summary>

**버그**: `rb_head++`에 모듈로가 없습니다. full 검사는 `% RB_N`을 하지만 `rb_head` 자체는 8, 9, …로 계속 커져서 `rb_buf[8]`부터 **버퍼 밖에 씁니다.** 처음 7개는 멀쩡하므로, pop으로 tail이 움직인 뒤 한 바퀴 돌 때 처음 터집니다. 테스트: push 7 → pop 7 → push(100) 하면 `rb_head == 8`.

**고친 코드**
```c
rb_buf[rb_head] = v;
rb_head = (rb_head + 1) % RB_N;
```
`RB_N`이 2의 거듭제곱이면 `& (RB_N - 1)`도 됩니다. 이 방식은 "한 칸 비우기"라 용량이 `RB_N − 1 = 7`이라는 것도 말합니다(대안: count 변수, 또는 free-running 인덱스 + 마스크 → [T2-1](../T2-1_ring_buffer/index.html)).

**어떻게 찾나**: 테스트가 고치지 않으면 다음 push가 `rb_buf[8]`에 쓰게 되는데, ASan은 이것을 `global-buffer-overflow ... WRITE of size 4`로 잡습니다. 여기 테스트는 그 쓰기를 하기 **전에** `rb_head < RB_N` 불변식을 검사해서 FAIL로 멈춥니다. 실기에서는 `rb_buf` 바로 뒤에 놓인 변수(대개 `rb_head`/`rb_tail` 자체)가 깨져서 "가끔 데이터가 이상하다"로 나타납니다.

</details>

### D4 · Reverse loop

**Intended behavior:** call `process(x[i])` for `i = n-1, n-2, …, 0`, each exactly once. `n == 0` must do nothing.

```c
for (size_t i = n - 1; i >= 0; i--) process(x[i]);
```

<details>
<summary>💡 D4 답 펼치기</summary>

**버그**: `size_t`는 부호가 없어서 `i >= 0`은 **항상 참**입니다. `i = 0`에서 `i--` 하면 `SIZE_MAX`가 되어 루프가 끝나지 않고 `x[SIZE_MAX]`를 읽습니다. `n == 0`이면 시작부터 `i = SIZE_MAX`입니다. (`buggy.c`의 `TEST GUARD`가 `i >= n`을 감지해서 멈추게 했을 뿐, 원래 코드는 무한 루프 + 잘못된 메모리 읽기입니다.)

**고친 한 줄**
```c
for (size_t i = n; i-- > 0; ) process(x[i]);
```
조건을 검사한 **다음에** 감소하므로 본문에서는 `n-1 … 0`이 보이고, `n == 0`이면 한 번도 돌지 않습니다. `int i`로 바꾸는 방법도 있지만 `n`이 `INT_MAX`보다 크면 다시 틀리고 `-Wsign-compare` 경고가 납니다.

**어떻게 찾나**: 컴파일러가 이미 알려 줍니다.
```
warning: result of comparison of unsigned expression >= 0 is always true
         [-Wtautological-unsigned-zero-compare]        (clang)
warning: comparison of unsigned expression in '>= 0' is always true [-Wtype-limits]   (gcc -Wextra)
```
그래서 펌웨어 빌드는 `-Wall -Wextra -Werror`가 기본이라고 말합니다.

</details>

### D5 · 12-bit sensor register

**Intended behavior:** a TMP102-style sensor returns a 12-bit two's-complement temperature: `msb` holds bits 11..4, the upper nibble of `lsb` holds bits 3..0. Return the signed raw count (1 count = 0.0625 °C). For example `0x190` → `+400` (+25 °C), `0xE70` → `-400` (−25 °C).

```c
int16_t raw = (msb << 4) | (lsb >> 4);     /* 12-bit two's complement */
return raw;
```

<details>
<summary>💡 D5 답 펼치기</summary>

**버그**: 12-bit 값을 16-bit 변수에 넣으면서 **부호 확장을 안 했습니다.** bit 11(부호 비트)이 1이어도 bit 15..12는 0으로 채워져서, 음수 온도가 큰 양수가 됩니다. −25 °C(`0xE70`) → **+3696** = 231.0 °C. 양수 온도는 정상이라 실온 테스트로는 절대 안 보입니다.

<div class="chart" data-src="chart2.svg" data-caption="D5: 버그 버전은 위 4비트가 0이라 +3696(231 °C). 부호 비트(bit 11, 보라)를 위 4비트에 복사해야 −400(−25 °C)"></div>

**고친 코드** (둘 중 하나)
```c
int16_t raw = (int16_t)((msb << 4) | (lsb >> 4));
if (raw & 0x800) raw -= 0x1000;                 /* sign-extend bit 11 */

int16_t raw = (int16_t)((msb << 8) | lsb) / 16; /* 16-bit로 놓고 나누기 */
```
두 번째 방법은 부호 비트를 bit 15에 맞춘 뒤 16으로 나눕니다. `>> 4`도 되지만 음수의 오른쪽 시프트는 implementation-defined라서, 면접에서는 `/ 16`이나 첫 번째 방법을 쓰고 그 이유를 말합니다(lsb 하위 니블이 0이라 `/ 16`은 정확히 나눠떨어짐).

**확인 값** (`fixed.c` 테스트): `0x19,0x00 → 400`, `0x7F,0xF0 → 2047`, `0xE7,0x00 → −400`, `0xFF,0xF0 → −1`, `0x80,0x00 → −2048`. 버그 버전은 뒤 세 개가 3696, 4095, 2048로 나옵니다.

**어떻게 찾나**: sanitizer는 못 잡습니다. **음수 경계값 테스트**(−0.0625 °C = `0xFFF`, 최소값 `0x800`)가 답입니다. 실기에서는 "겨울에 냉동 창고 테스트를 하니 과열 경보가 뜬다"로 나타납니다. 자세한 버전은 [T1-5](../T1-5_raw_register_to_temp/index.html).

</details>

### D6 · Counter shared with an ISR

**Intended behavior:** `TEMP_IRQHandler()` counts overheat events; `task()` (main loop) reports every event exactly once. No event may be lost, even if the IRQ fires at any instruction inside `task()`.

```c
static uint32_t overheat_count;                 /* incremented in ISR */
void TEMP_IRQHandler(void) { overheat_count++; }
void task(void) {
    if (overheat_count > 0) { report(overheat_count); overheat_count = 0; }
}
```

In `buggy.c` the line `irq_window();` marks where the test makes one interrupt fire, and `irq_disable()` / `irq_enable()` model `__disable_irq()` / `__enable_irq()` (an IRQ raised while masked stays pending and runs on enable).

<details>
<summary>💡 D6 답 펼치기</summary>

**버그 2개**
1. **읽기-지우기 경쟁(race)**: `report(overheat_count)`로 읽은 뒤 `overheat_count = 0` 사이에 ISR이 들어와 `count++`를 하면, 그 증가분을 0으로 덮어써서 **이벤트를 잃습니다.** 테스트: 이벤트 3개 → task가 3을 보고 → 그 사이 4번째 IRQ → 0으로 지움 → 보고된 합계 3, 남은 카운트 0.
2. **`volatile` 없음**: 컴파일러는 이 변수가 ISR에서 바뀐다는 것을 모릅니다. `-O2`에서 `while (overheat_count == 0) {}` 같은 대기 루프는 한 번만 읽고 무한 루프가 될 수 있고, 읽기·쓰기가 합쳐지거나 재배치될 수 있습니다. 단, `volatile`은 **"매번 메모리에 접근하라"**일 뿐 원자성을 주지 않습니다. 1번은 `volatile`로 안 고쳐집니다.

<div class="chart" data-src="chart3.svg" data-caption="D6: 위 — 읽기와 지우기 사이에 IRQ가 끼어들어 count++가 사라짐. 아래 — 인터럽트를 잠깐 막으면 IRQ는 pending으로 기다렸다가 enable 직후 실행되어 다음 루프에서 보고됨"></div>

**고친 코드**
```c
static volatile uint32_t overheat_count;

void task(void) {
    __disable_irq();                 /* 짧은 critical section: 읽고 지우기만 */
    uint32_t c = overheat_count;
    overheat_count = 0;
    __enable_irq();
    if (c > 0) report(c);            /* 느린 작업은 인터럽트 켠 뒤에 */
}
```
대안: C11 `atomic_exchange(&overheat_count, 0)` (Cortex-M3 이상은 LDREX/STREX로 구현), 또는 "ISR은 `produced`만 증가, task는 `consumed`만 증가, 차이를 보고"하는 단일 작성자 카운터 2개(락 없이 안전). `__disable_irq()` 대신 `PRIMASK`를 저장·복원하면 이미 꺼진 상태에서 불려도 안전합니다.

**어떻게 찾나**: ASan/UBSan은 못 잡습니다(단일 스레드에서는 경쟁이 없음). 리뷰에서 **"ISR과 공유하는 변수 = read-modify-write가 원자적인가?"**를 체크리스트로 봅니다. 실기 재현은 IRQ 빈도를 올리고(예: 테스트 타이머로 10 kHz) 카운트 합계를 비교하거나, GPIO 토글 + 로직 분석기로 critical section 길이를 잽니다.

</details>

### D7 · Zero a struct

**Intended behavior:** set every field of `*s` to 0.

```c
typedef struct { uint32_t t_ms; int16_t temp_c10; uint16_t flags; uint32_t seq; uint32_t crc; } Sample;
void clear(Sample *s) { memset(s, 0, sizeof(s)); }
```

<details>
<summary>💡 D7 답 펼치기</summary>

**버그**: `sizeof(s)`는 **포인터 크기**(64-bit 호스트 8, Cortex-M 4)입니다. `Sample`은 16바이트라서 앞 8바이트(`t_ms`, `temp_c10`, `flags`)만 지워지고 `seq`, `crc`는 그대로 남습니다. Cortex-M에서는 4바이트만 지워집니다. 즉 **플랫폼마다 다르게 틀립니다.**

**고친 한 줄**
```c
memset(s, 0, sizeof(*s));      /* 또는 *s = (Sample){0}; */
```
`*s = (Sample){0};`는 크기를 쓸 일이 없어서 더 안전합니다.

**어떻게 찾나**: clang은 기본 경고로 바로 알려 줍니다.
```
warning: 'memset' call operates on objects of type 'Sample' while the size is based
         on a different type 'Sample *' [-Wsizeof-pointer-memaccess]
```
같은 실수가 배열 파라미터에서도 납니다: `void f(int a[16]) { sizeof(a) }`도 포인터 크기입니다(배열이 함수 인자로 오면 포인터로 바뀜).

</details>

### D8 · Debounce counter

**Intended behavior:** enter `OVERHEAT` only after **3 consecutive** readings `>= HIGH` (800). Any reading below `HIGH` restarts the count.

```c
if (temp >= HIGH) {
    if (++d->hot_cnt >= 3) d->state = OVERHEAT;
}
```

<details>
<summary>💡 D8 답 펼치기</summary>

**버그**: 온도가 내려가도 `hot_cnt`를 **리셋하지 않습니다.** 그래서 "연속 3회"가 아니라 "누적 3회"가 됩니다. 테스트: `900, 700, 900, 700, 900` → 떨어져 있는 스파이크 3개로 과열 판정. `900, 900, 799, 900`도 마찬가지(799 < 800).

**고친 한 줄**
```c
} else d->hot_cnt = 0;
```
추가로 `++hot_cnt`는 `uint8_t`라 256번째에 0으로 돌아갑니다. 여기서는 상태가 이미 래치되어 무해하지만, 같은 카운터로 "N회 연속이면 알람 유지" 같은 판정을 하면 문제가 되므로 `if (hot_cnt < 3) hot_cnt++;`로 포화시키는 것이 좋습니다(`fixed.c`가 그렇게 함).

**어떻게 찾나**: 도구는 못 잡습니다. **"끊어진 스파이크" 테스트 벡터**를 넣어야 합니다. 이 패턴의 완전한 버전(히스테리시스 + 회복)은 [T1-2](../T1-2_streaming_hysteresis/index.html), [T2-7](../T2-7_debounce_edge/index.html).

</details>

### 4.2 정리 표

| Drill | bug class | 증상 | 고치는 한 줄 |
|---|---|---|---|
| D1 | integer overflow + divide by zero | 큰 입력에서 평균 308(기대 8500), `n == 0`에서 UB | `int32_t sum;` + `if (n == 0) return AVG_INVALID;` |
| D2 | wraparound (add-compare) | 랩 직전 start면 16 tick 만에 타임아웃 | `return (uint32_t)(now - start) >= timeout;` |
| D3 | index not wrapped | 한 바퀴 돌면 `rb_buf[8]`에 씀 | `rb_head = (rb_head + 1) % RB_N;` |
| D4 | unsigned loop underflow | 무한 루프, `x[SIZE_MAX]` 읽기 | `for (size_t i = n; i-- > 0; )` |
| D5 | missing sign extension | −25 °C가 231 °C로 읽힘 | `if (raw & 0x800) raw -= 0x1000;` |
| D6 | ISR race + missing `volatile` | 읽기와 지우기 사이 이벤트 유실 | `volatile` + `__disable_irq(); c = cnt; cnt = 0; __enable_irq();` |
| D7 | `sizeof` pointer | 앞 8바이트만 지워짐 | `memset(s, 0, sizeof(*s));` |
| D8 | state not reset | 끊어진 스파이크 3개로 과열 판정 | `else d->hot_cnt = 0;` |

### 4.3 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (드릴 다 해 본 다음에)</summary>

<div class="code" data-src="fixed.c"></div>

</details>

**코드 읽는 포인트**
- 모든 수정에 `/* FIX: ... */` 주석이 붙어 있습니다. `diff buggy.c fixed.c`로 보면 드릴마다 1–5줄만 다릅니다.
- D1의 `n == 0` 테스트는 `fork()`한 자식 프로세스에서 돌립니다. x86에서는 0으로 나누기가 SIGFPE로 프로세스를 죽이기 때문에, 버그 버전에서도 테스트 전체가 멈추지 않게 하려는 장치입니다. 문제 자체와는 무관합니다.
- D6의 `irq_window()`는 fixed 버전에도 같은 자리에 남아 있습니다. 같은 타이밍에 IRQ가 와도 이제는 pending으로 기다렸다가 `irq_enable()`에서 실행된다는 것을 테스트가 보여 줍니다.

### 4.4 복잡도

드릴 자체는 복잡도 문제가 아닙니다. 고친 코드는 모두 원래와 같은 O(n) 또는 O(1)이고, 추가 메모리도 없습니다(D6의 critical section은 명령어 몇 개 길이).

---

## 5. 임베디드 관점으로 말할 것

- **경고를 에러로**: D4, D7은 `-Wall -Wextra -Werror`만으로 빌드가 막힙니다. "우리 펌웨어 빌드는 경고 0이 기본"이라고 말하면 좋습니다. 정적 분석(clang-tidy, cppcheck, MISRA 검사기)은 D1의 좁은 누산기, D5의 암시적 변환도 지적합니다.
- **호스트에서 sanitizer로 먼저**: 펌웨어 로직(D1, D3, D4)은 HAL과 분리해서 PC에서 ASan/UBSan으로 단위 테스트합니다. 타깃에서는 MPU로 스택/버퍼 경계를 막고, HardFault 핸들러에서 PC/LR을 로그로 남깁니다.
- **도구가 못 잡는 것은 테스트 벡터로**: D2(랩 직전 tick), D5(음수 온도), D8(끊어진 스파이크)는 경계값을 의도적으로 넣는 테스트로만 드러납니다. tick 카운터 초기값을 랩 직전으로 두는 것은 실제로 많이 쓰는 기법입니다.
- **ISR 공유 변수 규칙**: `volatile`은 가시성, critical section/atomic은 원자성. 둘은 다른 문제라는 것을 구분해서 말하면 시니어로 보입니다. critical section은 짧게(읽고 지우기만), 느린 `report()`는 밖에서.
- **고정폭 정수와 단위**: `int16_t`의 0.01 °C 단위는 ±327 °C까지만 됩니다. 누산·곱셈 전에 "최대 입력 × 최대 개수"가 몇 비트인지 계산하는 습관이 D1을 막습니다.

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Does `volatile` alone fix D6? | 아닙니다. `volatile`은 컴파일러가 접근을 생략·캐시하지 않게 할 뿐이고, 읽기→지우기 두 동작 사이의 끼어들기는 막지 못합니다. 원자성은 critical section이나 `atomic_exchange`가 줍니다. |
| Can you fix D6 without disabling interrupts? | ISR은 `produced++`만, task는 `consumed`만 씁니다. 보고할 양은 `produced - consumed`(unsigned 뺄셈이라 랩도 안전). 각 변수의 작성자가 하나뿐이고 32-bit 정렬 읽기/쓰기는 Cortex-M에서 원자적이라 락이 필요 없습니다. 또는 `LDREX/STREX` 기반 `atomic_exchange`. |
| How long can the D2 timeout be? | 차이를 `uint32_t`로 보므로 2^32 − 1 tick까지. 1 ms tick이면 약 49.7일. 그보다 긴 타임아웃은 64-bit tick이나 별도 카운터가 필요합니다. 부호 있는 비교 `(int32_t)(now - deadline) >= 0` 방식은 절반(약 24.8일)까지입니다. |
| Why not just make the D1 accumulator `int64_t`? | 가능하지만 Cortex-M0에서는 64-bit 덧셈이 여러 명령어라 느립니다. 필요한 폭을 계산하면 `uint8_t n` × `int16_t` 최대 = 255 × 32767 < 2^23이라 `int32_t`로 충분합니다. 폭은 계산해서 고릅니다. |
| How would you find D3 on the target, without ASan? | 버퍼 뒤에 canary 값을 두고 주기적으로 검사, MPU로 버퍼 끝 다음 영역을 no-access로 설정, 또는 디버거의 data watchpoint(DWT)를 `&rb_buf[RB_N]`에 걸어 쓰는 순간 멈추게 합니다. |
| In D5, is `(int16_t)(raw << 4) >> 4` a good fix? | 흔히 쓰지만 음수의 오른쪽 시프트는 implementation-defined이고, 범위를 넘는 값을 `int16_t`로 변환하는 것도 implementation-defined입니다. 실제 컴파일러(GCC/Clang/ARMCC)는 모두 산술 시프트라 동작은 하지만, 면접에서는 `if (raw & 0x800) raw -= 0x1000;`처럼 표준만으로 정의된 방법을 먼저 말합니다. |

## 7. 자주 하는 실수

- D6에서 **`volatile`만 붙이고 끝냄** → 경쟁은 그대로. 테스트 `g_reported == 4`가 계속 FAIL
- D6에서 `report()`까지 critical section 안에 넣음 → 동작은 맞지만 인터럽트 지연이 길어져 다른 IRQ(모터 제어 등)가 밀림
- D4를 `int i = n - 1; i >= 0`으로 고침 → `n`이 `size_t`라 `-Wsign-compare` 경고, 큰 `n`에서 다시 틀림
- D2를 `now - start > timeout`으로 고침 → 랩은 해결됐지만 경계(`== timeout`)에서 한 tick 늦음. 테스트 `timed_out(600, 100, 500)` FAIL
- D3을 고치면서 full 검사 `(rb_head + 1) % RB_N == rb_tail`까지 바꿔 버림 → 그 줄은 원래 맞습니다
- D1에서 오버플로만 고치고 `n == 0`을 잊음 (또는 반대) — 버그가 **두 개**라는 것을 먼저 말할 것
- D7을 `sizeof(Sample)`로 고침 → 지금은 맞지만, 나중에 `s`의 타입이 바뀌면 다시 틀림. `sizeof(*s)`가 더 견고
