# A2. 메모리와 비트 — 스택·힙·정렬·비트 연산

> **이 노트를 읽고 나면**
> - `struct {uint64_t ts; float lux;}`가 왜 12가 아니라 16바이트인지 종이에 계산해 보인다
> - "10분치 히스토리는 몇 KB인가"를 샘플레이트 × 시간 × 크기로 즉석에서 답한다
> - `head & (CAP-1)`이 `head % CAP`와 같아지는 조건을 말하고, 두 값을 `uint64_t` 하나에 pack/unpack 한다
> **선행**: [A1. C 기초 다시 세우기](A1_c_refresher.md)
> **이 개념을 쓰는 문제**: 10문제 전부의 링버퍼 인덱싱. 특히 [02_modem_rssi_window](../02_modem_rssi_window/question_note) 의 pack된 publication word, [05_frame_latest_and_replay](../05_frame_latest_and_replay/question_note) 의 바이트 예산, [01_gps_fix_cache](../01_gps_fix_cache/question_note) 의 40바이트 struct.

---

## 1. 왜 이게 필요한가

면접관이 [01_gps_fix_cache](../01_gps_fix_cache/question_note) 지문 맨 아래에서 이렇게 묻는다.

```
6. Part 2: how much memory is 10 minutes? What is the worst-case fix rate,
   and what happens when your buffer fills anyway?
```

동시성 질문이 아니다. **구조체 하나가 몇 바이트인지, 그걸 몇 개 잡을지** 를 그 자리에서 계산하라는 것이다.
못 하면 "malloc으로 필요할 때 늘리죠"라고 답하게 되고, 임베디드 면접에서 그건 감점이다.

그리고 10문제 전부의 링버퍼가 `g_hist.buf[g_hist.head & HIST_MASK]` 한 줄로 인덱싱한다.
`HIST_MASK`는 `GPS_HIST_CAP - 1`이다. 왜 `% CAP`를 안 쓰는지, 왜 용량이 2의 거듭제곱이어야 하는지,
`head`가 `uint32_t` 최대를 넘어 wrap 해도 왜 안전한지 — 여기를 모르면 모범답안이 마술처럼 보인다.

[02_modem_rssi_window/rssi_window_solution.c](../02_modem_rssi_window/rssi_window_solution.c) 는 한 발 더 나간다.
RSSI(정수)와 타임스탬프(48비트)를 **`uint64_t` 하나에 밀어 넣어** 원자적 publish를 공짜로 만든다.
시프트와 마스크가 손에 없으면 그 코드는 읽히지도 않는다.

## 2. 그림으로 먼저

프로세스 메모리는 네(다섯) 구역이다. 그리고 **스택은 스레드마다 따로다** — 이게 B 트랙 전체의 토대다.

```svg
<svg viewBox="0 0 620 280" role="img" aria-label="프로세스 메모리 구역과 스레드별 스택">
  <text class="lbl" x="10" y="16">높은 주소  ·  스택은 스레드마다 따로</text>
  <rect class="box" x="60" y="26" width="150" height="46" rx="6"/><text x="135" y="54" text-anchor="middle">스택 (main)</text>
  <rect class="box" x="235" y="26" width="150" height="46" rx="6"/><text x="310" y="54" text-anchor="middle">스택 (sampler)</text>
  <rect class="box" x="410" y="26" width="150" height="46" rx="6"/><text x="485" y="54" text-anchor="middle">스택 (reader N)</text>
  <text class="lbl" x="60" y="92">지역변수 · 인자 · 복귀주소   ·   pthread_create 가 스레드마다 잡는다 (512 KiB ~ 8 MiB)</text>
  <text class="lbl" x="60" y="110">↕ 남의 스택은 안 보인다 (주소를 넘기면 보인다)</text>
  <rect class="fill-soft" x="60" y="120" width="500" height="40" rx="6"/><text x="310" y="145" text-anchor="middle">힙  —  malloc / free, 수명은 내가 관리</text>
  <rect class="fill-soft" x="60" y="170" width="500" height="46" rx="6"/><text x="310" y="192" text-anchor="middle">BSS  —  초기값 없는 static/전역, 0으로 채워짐</text>
  <text class="lbl" x="310" y="209" text-anchor="middle">static hist_entry_t buf[8192] 가 여기 있다</text>
  <rect class="fill-soft" x="60" y="226" width="500" height="30" rx="6"/><text x="310" y="246" text-anchor="middle">data  —  초기값 있는 static/전역</text>
  <rect class="box" x="60" y="260" width="500" height="18" rx="4"/><text class="lbl" x="310" y="273" text-anchor="middle">text (코드, 읽기 전용) · 낮은 주소</text>
  <line class="dash" x1="135" y1="74" x2="135" y2="116"/><line class="dash" x1="310" y1="74" x2="310" y2="116"/><line class="dash" x1="485" y1="74" x2="485" y2="116"/>
</svg>
```

전역/static 구역과 힙은 **모든 스레드가 공유한다**. 스택은 공유하지 않는다.
그래서 여러 스레드가 다투는 대상은 항상 전역/static이나 힙에 있는 데이터다 — 지역변수는 다툴 일이 없다.
실제로 주소를 찍어 보면 구역이 갈린다. macOS arm64에서 돌린 출력:

```
g_data(data)  0x102604004      s_local(data) 0x102604000
g_bss (bss)   0x10260400c      heap          0x102c91c30
local(stack)  0x16d8028fc
```

data/BSS가 붙어 있고 힙이 그보다 위, 스택은 완전히 다른 대역이다.
구체적 숫자는 실행마다 바뀌므로 **상대적 배치**만 보면 된다.

## 3. 개념 (용어를 하나씩)

### 수명(lifetime)과 스코프(scope)

**스코프** = 이름이 보이는 범위(컴파일 시간). **수명** = 그 메모리가 유효한 기간(실행 시간). 둘은 다르다.
함수 안 `static int n`은 스코프가 함수뿐인데 수명은 프로그램 전체다. 지역변수 `int x`는 스코프도
수명도 그 블록뿐이고, **함수가 반환하면 그 스택 프레임은 즉시 재사용 대상**이다.

### 지역 변수 주소를 넘기면 왜 위험한가

```c
static const char *bad_name(void)
{
    char buf[16];
    snprintf(buf, sizeof buf, "trailer-7");
    return buf;                /* buf 는 이 줄 다음에 죽는다 */
}
```

clang이 `-Wreturn-stack-address`로 잡아준다: "address of stack memory associated with
local variable 'buf' returned". **돌려주면** 안 되는 것이고, **호출이 끝나기 전에 쓰고 버리면**
완전히 안전하다. `gps_get_last_fix(&f)`의 `f`는 호출자 스택의 지역변수지만 호출 중에만 쓰이므로
문제없다. 위험한 경우는 딱 둘 — 주소를 **반환**하거나, 주소를 **다른 스레드에 남겨 두고** 함수가 끝나는 것.

그래서 문제들이 `static` 고정 배열을 쓴다.

```c
static struct {
    pthread_mutex_t lock;
    hist_entry_t    buf[GPS_HIST_CAP];
    uint32_t        head, tail, overflow;
} g_hist = { .lock = PTHREAD_MUTEX_INITIALIZER };
```

이유 세 개. (1) 수명이 프로그램 전체라 sampler와 reader 스레드가 같이 봐도 안전하다.
(2) 크기가 링크 시점에 확정되어 **런타임에 메모리가 부족해질 수 없다** — 임베디드에서 이게 핵심이다.
(3) malloc/free가 없으니 use-after-free가 원천적으로 없다.

### `sizeof`, 정렬, 패딩

`sizeof x` = x가 차지하는 바이트 수, **컴파일 시간에 결정**. 타입은 `size_t`, 출력은 `%zu`.
`sizeof arr / sizeof arr[0]`이 원소 개수인데 **진짜 배열일 때만** 맞다([A1](A1_c_refresher.md) 참고).

**정렬(alignment)** = "이 타입은 주소가 N의 배수여야 한다"는 요구. `_Alignof(T)`로 확인한다.
`uint8_t`는 1, `uint32_t`/`float`은 4, `uint64_t`/`double`/포인터는 8이다. CPU가 정렬된 주소에서
훨씬 빠르게(어떤 아키텍처에서는 **유일하게**) 읽기 때문이고, 8바이트 정렬은 원자적 연산의
전제조건이기도 하다 — 이건 B 트랙에서 다시 나온다.

**패딩(padding)** = 다음 필드의 정렬을 맞추려고 컴파일러가 끼우는 빈 바이트. 규칙 둘이 크기를 정한다.
- 각 필드는 **자기 정렬의 배수** 위치에 놓인다.
- **구조체 전체의 정렬은 가장 큰 필드 정렬**이고, **구조체 크기는 그 정렬의 배수**여야 한다.

두 번째 규칙이 "tail padding"의 이유다. 배열로 늘어놓았을 때 두 번째 원소도 정렬되어야 하니까.

### 링버퍼와 `& (N-1)`

`N`이 2의 거듭제곱이면 `x & (N-1)`은 `x % N`과 **부호 없는 정수에 대해** 항상 같다.
`N = 2^k`에서 `x % N`은 **x의 하위 k비트**이고, `N-1`은 하위 k비트가 전부 1인 마스크다.

```
CAP = 8192 = 2^13
CAP - 1     = 0001_1111_1111_1111   (하위 13비트가 1)
x & (CAP-1) = x 의 하위 13비트      = x % 8192
```

왜 쓰는가: `%`는 나눗셈 명령(수십 사이클, 임베디드 코어에서는 더)이고 `&`는 1사이클이다. 그리고
`head`가 `uint32_t` 최대를 넘어 wrap 해도 `2^32`가 `8192`의 배수이므로 슬롯 순서가 안 깨진다.
**부호 없는 타입이 조건이다** — `int`는 음수에서 `%`가 음수를 주고 오버플로 자체가 UB다.

```c
_Static_assert((GPS_HIST_CAP & (GPS_HIST_CAP - 1u)) == 0u,
               "GPS_HIST_CAP must be a power of two");
```

### 비트 연산 네 가지

```c
#define BIT(n) (1u << (n))
#define FLAG_VALID BIT(0)
#define FLAG_STALE BIT(1)

uint32_t f = 0;
f |= FLAG_VALID;                   /* set    — OR 1 은 켜고 나머지는 보존   */
f &= ~FLAG_STALE;                  /* clear  — ~mask 는 그 비트만 0        */
f ^= FLAG_VALID;                   /* toggle — XOR 1 은 반전, XOR 0 은 보존 */
int valid = (f & FLAG_VALID) != 0; /* test   — AND 로 그 비트만 남긴다      */
```

**마스크(mask)** = 관심 있는 비트만 1인 값. `0x7`은 하위 3비트 마스크(`0xAB & 0x7 == 0x3`).
`test`에 `!= 0`을 붙이는 이유: `f & BIT(3)`은 `8`이지 `1`이 아니다. bool로 쓰려면 비교가 필요하다.
`x << n`은 `x * 2^n`, 부호 없는 `x >> n`은 `x / 2^n`이다. 둘 다 `*`나 `/`보다 싸다.

### 시프트의 함정 세 개

- **음수의 `>>`는 산술 시프트**로 부호 비트가 복제된다: `-8 >> 1 == -4`. 부호 없는 값은 0을 채운다.
- **시프트 양은 타입의 비트 수보다 작아야 한다.** `1 << 40`은 `int` 시프트라 UB. `(uint64_t)1 << 40`.
- **`1 << 31`도 `int`에서는 UB.** 플래그 비트는 항상 `1u << 31`.

### pack / unpack

한 워드에 여러 값을 넣는 것. 왜: **64비트 원자적 store 하나로 두 값을 동시에 publish** 할 수 있다.
따로 두면 reader가 "새 rssi + 옛 timestamp"를 볼 수 있는데, 한 워드면 그게 불가능하다.

```
uint64_t word
 63                                   16 15            0
┌──────────────────────────────────────┬────────────────┐
│  rel_us  (t0 로부터의 상대 시각 48비트)  │  rssi 16비트    │
└──────────────────────────────────────┴────────────────┘
  pack   : (rel_us << 16) | (uint16_t)(int16_t)rssi
  unpack : rel_us = word >> 16
           rssi   = (int16_t)(uint16_t)(word & 0xFFFF)
```

48비트면 `281474976710655 us` = 약 **8.9년**의 uptime을 담는다. 그래서 상대 시각으로 바꾼 것이다
(절대 us는 1970년부터 세므로 48비트에 안 들어간다). 넘칠 때는 clamp한다.
부호 있는 값을 pack할 때 **캐스트가 두 번** 들어가는 이유: `(int16_t)`로 16비트 부호 있는 값임을
정하고 `(uint16_t)`로 부호 확장 없이 16비트만 꺼낸다. `(uint64_t)(int16_t)(-97)`은 상위 48비트가
전부 1이 되어 rel 필드를 망친다.

### 엔디안(endianness)

여러 바이트 정수를 메모리에 놓는 순서. **little-endian** = 낮은 주소에 하위 바이트(x86, 보통의 ARM),
**big-endian** = 낮은 주소에 상위 바이트(네트워크 바이트 순서).

**언제 신경 쓰나**: 정수를 **바이트 배열로 꺼내거나 바이트에서 조립할 때만**.
즉 네트워크 패킷, 파일 포맷, 레지스터 덤프다. 프로그램 안에서 `uint32_t`로 계산만 하는 동안은 상관없다.
해법은 하나 — **바이트 순서를 코드에 명시한다.**

```c
static uint32_t be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] <<  8) | ((uint32_t)p[3]);
}
/* be32("\x00\x00\x1f\x40") -> 8000.  기계 엔디안과 무관하다. */
```

### `volatile` (여기서는 맛보기만)

`volatile`은 컴파일러에게 **"이 변수는 내가 모르는 사이에 바뀔 수 있으니 최적화로 읽기를 없애지 말라"** 고 말한다.
없으면 `while (!flag) {}` 가 `flag`를 레지스터에 한 번 읽고 무한 루프로 컴파일될 수 있다.

**여기까지가 전부다.** `volatile`은 원자성도, 메모리 순서도, 스레드 간 가시성도 보장하지 않는다.
그래서 모범답안들은 `volatile`이 아니라 `_Atomic` + `memory_order`를 쓴다. 왜 부족한지는 B6에서
제대로 다룬다. 지금은 **"컴파일러 최적화 억제, 그 이상은 아니다"** 만 기억한다.

## 4. 코드로 보기

정렬과 패딩을 직접 재 본다.

```c
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

struct LuxSample { uint64_t ts; float lux; };   /* 원본 ALS 문제의 샘플 */
struct LuxPacked { float lux; uint64_t ts; };   /* 순서만 바꿔 본 것    */

int main(void)
{
    printf("LuxSample size=%zu align=%zu off(ts)=%zu off(lux)=%zu\n",
           sizeof(struct LuxSample), _Alignof(struct LuxSample),
           offsetof(struct LuxSample, ts), offsetof(struct LuxSample, lux));
    printf("LuxPacked size=%zu align=%zu off(lux)=%zu off(ts)=%zu\n",
           sizeof(struct LuxPacked), _Alignof(struct LuxPacked),
           offsetof(struct LuxPacked, lux), offsetof(struct LuxPacked, ts));
    printf("8192 * %zu B = %zu KiB\n",
           sizeof(struct LuxSample), 8192u * sizeof(struct LuxSample) / 1024u);
    return 0;
}
```

돌려본 출력:

```
LuxSample size=16 align=8 off(ts)=0 off(lux)=8
LuxPacked size=16 align=8 off(lux)=0 off(ts)=8
8192 * 16 B = 128 KiB
```

`8 + 4 = 12`인데 왜 16인가. 그림으로:

```svg
<svg viewBox="0 0 600 230" role="img" aria-label="구조체 패딩 계산">
  <text class="lbl" x="10" y="16">struct LuxSample { uint64_t ts; float lux; }   →  16바이트</text>
  <rect class="fill-soft" x="40" y="26" width="280" height="38" rx="4"/><text x="180" y="50" text-anchor="middle">ts (uint64_t, 8B)</text>
  <rect class="box" x="320" y="26" width="140" height="38" rx="4"/><text x="390" y="50" text-anchor="middle">lux (float, 4B)</text>
  <rect class="box" x="460" y="26" width="140" height="38" rx="4"/><text class="lbl" x="530" y="50" text-anchor="middle">패딩 4B</text>
  <text class="lbl" x="40" y="80">0</text><text class="lbl" x="316" y="80">8</text><text class="lbl" x="454" y="80">12</text><text class="lbl" x="586" y="80">16</text>
  <text class="lbl" x="10" y="116">struct LuxPacked { float lux; uint64_t ts; }   →  역시 16바이트</text>
  <rect class="box" x="40" y="126" width="140" height="38" rx="4"/><text x="110" y="150" text-anchor="middle">lux (4B)</text>
  <rect class="box" x="180" y="126" width="140" height="38" rx="4"/><text class="lbl" x="250" y="150" text-anchor="middle">패딩 4B</text>
  <rect class="fill-soft" x="320" y="126" width="280" height="38" rx="4"/><text x="460" y="150" text-anchor="middle">ts (uint64_t, 8B)</text>
  <text class="lbl" x="40" y="180">0</text><text class="lbl" x="176" y="180">4</text><text class="lbl" x="316" y="180">8</text><text class="lbl" x="586" y="180">16</text>
  <text class="lbl" x="10" y="212">규칙 1: 각 필드는 자기 정렬의 배수 위치.   규칙 2: 구조체 크기는 최대 정렬(8)의 배수.</text>
</svg>
```

- `LuxSample`: ts가 0~7, lux가 8~11. 여기서 끝내면 크기가 12인데 12는 8의 배수가 아니다.
  배열 `buf[2]`에서 `buf[1].ts`가 주소 12에 놓여 8바이트 정렬이 깨진다. 그래서 **tail padding 4바이트**로 16.
- `LuxPacked`: lux가 0~3, 다음 ts는 8의 배수 위치여야 하니 4~7이 패딩, ts가 8~15. 역시 16.
- 4바이트 하나 + 8바이트 하나면 어느 순서든 한 번은 4바이트를 버린다.
  **"큰 필드부터"** 규칙은 필드가 더 많을 때 효과가 있다.

`struct GpsFix`도 같은 방식으로 40이 된다. 직접 재 본 값은 `size=40 align=8 off(lat)=8 off(hdop)=24 off(ts)=32`.

```
off  0  status    int      4B
off  4  (패딩)             4B   ← 다음 double 을 8의 배수에 놓으려고
off  8  lat       double   8B
off 16  lon       double   8B
off 24  hdop      float    4B
off 28  (패딩)             4B   ← 다음 uint64_t 를 8의 배수에 놓으려고
off 32  timestamp uint64_t 8B
        합계 40 (8의 배수, tail padding 없음)
```

40바이트 중 8바이트가 패딩이다. 그리고 **패딩 바이트의 내용은 불확정**이라서, 구조체 두 개를
`memcmp`로 비교하면 값이 같아도 다르다고 나올 수 있다.
([09_wifi_scan_snapshot/wifi.h](../09_wifi_scan_snapshot/wifi.h) 의 `struct ApInfo`는 전 필드가 1바이트 정렬이라
패딩이 없고 `sizeof == 41`. 그래서 주석이 "memcmp/memcpy 해도 안전하다"고 명시한다. 재 봐도 41이다.)

## 5. 단계별로 만들어 보기

"10분치 히스토리 버퍼"의 크기를 세 번에 걸쳐 정한다.

**v0 — 틀렸다. 감으로 잡는다.**

```c
#define HIST_CAP 10000
static lux_sample_t buf[HIST_CAP];      /* 10000 * 16 = 160000 B = 156 KiB */
```

틀린 점 둘. (1) 근거가 없다 — 면접관이 "왜 10000?"이라 물으면 답이 없다.
(2) 2의 거듭제곱이 아니라 인덱싱에 `%`가 필요하다.

**v1 — 예산을 계산한다. 공식은 하나다.**

```
샘플레이트 × 보관 시간 × 항목 크기 = 필요 바이트

ALS 원본:   10 Hz × 600 s × 16 B  =  96000 B  ≈ 94 KiB   (6000개)
GPS 01번:   10 Hz × 600 s × 48 B  = 288000 B  ≈ 281 KiB  (6000개)
```

`10 Hz`는 최악의 fix rate, `600 s`는 `GPS_WINDOW_US`, 항목 크기는 §4에서 재 봤다. GPS쪽 48 B는
`struct GpsFix` 40 + prefix sum용 `double cum` 8 — 정렬이 8이라 패딩 없이 딱 48이다.

**v2 — 2의 거듭제곱으로 올림하고, 넘쳤을 때를 정한다.**

```c
/* 최악 10 Hz -> 10분에 6000개. 2의 거듭제곱으로 올려 인덱스를 마스크로:
 * 8192 * 48 B = 384 KiB. */
#define GPS_HIST_CAP 8192u
_Static_assert((GPS_HIST_CAP & (GPS_HIST_CAP - 1u)) == 0u,
               "GPS_HIST_CAP must be a power of two");
#define HIST_MASK (GPS_HIST_CAP - 1u)
```

6000 → 8192로 올리면서 **36% 더 잡는다**. 그 값으로 사는 것: `%`가 `&`가 되고, 센서가 예상보다
빨라도 여유가 생긴다. 이 트레이드오프를 말로 설명할 수 있어야 한다.
그리고 넘쳤을 때의 정책을 **명시적으로** 쓴다 — 조용히 버리면 나중에 원인을 못 찾는다.

```c
if (n == GPS_HIST_CAP) {     /* 윈도 만료로도 안 비었다 = 모듈이 예상보다 빠르다 */
    g_hist.tail++;
    g_hist.overflow++;       /* 조용히 버리지 않는다. 셀 수 있게 해 둔다 */
    n--;
}
```

인덱싱이 왜 안전한지는 이 그림으로 끝난다.

```
CAP = 8 (설명용), MASK = 7
head:  ... 6    7    8    9   10  ...     head 는 free-running (절대 리셋 안 함)
slot:      6    7    0    1    2          slot = head & 7

buf:  [0][1][2][3][4][5][6][7]            살아 있는 개수 n = head - tail
       ↑           ↑                      (unsigned 뺄셈이라 wrap 안전)
      tail&7      head&7

head 가 2^32 를 넘어 0 으로 돌아도 2^32 % 8 == 0 이므로 slot 순서가 안 깨진다.
```

`head - tail`을 `uint32_t`로 계산하는 게 핵심이다. `head`가 wrap해서 `tail`보다 작아져도
부호 없는 뺄셈은 올바른 개수를 준다. 모범답안이 `unsigned: wrap-safe` 라고 주석을 달아 둔 그 줄이다.

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
|---|---|---|---|
| 지역 배열의 주소를 반환 | 값이 랜덤하게 깨짐, 호출 한 번 끼면 바뀜 | 스택 프레임이 이미 재사용됨 | `static` 배열로 바꾸거나 호출자가 버퍼를 준다 |
| 구조체 크기를 필드 합으로 계산 | 12로 알았는데 16, 예산·오프셋이 다 틀림 | 정렬 패딩 + tail padding | `sizeof`로 재고 `offsetof`로 확인 |
| `memcmp`로 구조체 비교 | 값이 같은데 다르다고 나옴 | 패딩 바이트 내용이 불확정 | 필드별로 비교하거나 패딩 없는 타입만 memcmp |
| `CAP`가 2의 거듭제곱이 아닌데 `& (CAP-1)` | 인덱스가 범위 밖 또는 슬롯이 겹침 | 마스크 트릭의 전제가 깨짐 | `_Static_assert((CAP & (CAP-1)) == 0)` |
| `head`/`tail`을 `int`로 선언 | wrap 이후 개수가 음수, 인덱스가 음수 | 부호 있는 오버플로는 UB, `%`도 음수를 준다 | 전부 `uint32_t` |
| `1 << 31` 또는 `1 << 40` | 플래그가 0, 또는 UB로 최적화가 망가짐 | `int` 시프트, 타입 폭 초과 | `1u << 31`, `(uint64_t)1 << 40` |
| `if (f & BIT(3))` 을 `== 1`로 비교 | 항상 false | `f & BIT(3)`은 8이지 1이 아니다 | `!= 0` 으로 비교 |
| 음수를 `(uint64_t)`로 바로 캐스트해 pack | 상위 비트가 전부 1이 되어 다른 필드 오염 | 부호 확장 | `(uint64_t)(uint16_t)(int16_t)v` |
| `volatile`로 스레드 공유 플래그 해결 | TSan 경고, 드물게 값이 안 보임 | `volatile`은 최적화만 막는다 | `_Atomic` + `memory_order` (B 트랙) |
| 히스토리를 `malloc`으로 필요할 때 늘림 | 며칠 뒤 OOM, 최악 사용량을 모름 | 임베디드에서는 최악값이 예산이다 | 고정 크기 + overflow 카운터 |

## 7. 손으로 확인하기

구조체 크기와 마스크 등식을 한 파일에서 직접 재 본다. 저장소 루트에서:

```sh
cat > /tmp/sz.c <<'END'
#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include "01_gps_fix_cache/gps.h"
#include "09_wifi_scan_snapshot/wifi.h"
#define CAP 8192u
int main(void) {
    printf("GpsFix %zu (lat %zu, hdop %zu, ts %zu)\n", sizeof(struct GpsFix),
           offsetof(struct GpsFix, lat), offsetof(struct GpsFix, hdop),
           offsetof(struct GpsFix, timestamp));
    printf("ApInfo %zu align %zu\n", sizeof(struct ApInfo), _Alignof(struct ApInfo));
    for (uint32_t i = 0; i < 1000000u; i++) assert((i & (CAP-1u)) == (i % CAP));
    printf("8190..8194 -> %u %u %u %u %u\n", 8190u & (CAP-1u), 8191u & (CAP-1u),
           8192u & (CAP-1u), 8193u & (CAP-1u), 8194u & (CAP-1u));
    return 0; }
END
cc -std=c11 -Wall -Wextra -I. -o /tmp/sz /tmp/sz.c && /tmp/sz
```

나오는 값: `GpsFix 40 (lat 8, hdop 24, ts 32)` · `ApInfo 41 align 1` ·
`8190..8194 -> 8190 8191 0 1 2` (8192에서 0으로 감싼다. assert가 100만 번 다 통과한다).

pack과 메모리 예산이 실제 코드에 어떻게 적혀 있는지 본다.

```sh
grep -n "PUB_RSSI_BITS\|>> PUB" 02_modem_rssi_window/rssi_window_solution.c
grep -n "8192\|_Static_assert" 01_gps_fix_cache/gps_cache_solution.c
grep -n "KiB\|BYTES_MAX\|REPLAY_CAP" 05_frame_latest_and_replay/frame_store_solution.c
```

## 8. 자가 점검

```check
Q: `struct {uint64_t ts; float lux;}` 는 왜 12가 아니라 16바이트인가?
A: ts가 0~7, lux가 8~11까지 차서 내용은 12바이트다. 그런데 구조체 크기는 자기 정렬(가장 큰 필드 uint64_t의 8)의 배수여야 한다. 12는 8의 배수가 아니므로 tail padding 4바이트가 붙어 16이 된다. 이 규칙이 없으면 배열의 두 번째 원소부터 ts가 8바이트 정렬을 잃는다.

Q: 필드 순서를 `{float lux; uint64_t ts;}` 로 바꾸면 줄어드는가?
A: 안 줄어든다. lux가 0~3을 쓰고 ts는 8의 배수 위치여야 하므로 4~7이 패딩이 되어 역시 16이다. 4바이트 하나와 8바이트 하나면 어느 순서든 4바이트를 버린다. "큰 필드부터" 규칙은 필드가 더 많을 때 효과가 있다.

Q: 10분치 GPS 히스토리는 몇 바이트인가? 계산 과정을 말해 보라.
A: 샘플레이트 × 시간 × 항목 크기다. 최악 10 Hz × 600 s = 6000개, 항목은 struct GpsFix 40 B + double cum 8 B = 48 B라서 288000 B, 약 281 KiB다. 개수를 2의 거듭제곱 8192로 올리면 8192 × 48 = 384 KiB가 실제로 잡는 양이다.

Q: `head & (CAP-1)` 이 `head % CAP` 와 같아지는 조건은?
A: CAP가 2의 거듭제곱이고 head가 부호 없는 타입일 때다. CAP = 2^k면 나머지는 하위 k비트이고 CAP-1이 정확히 그 하위 k비트 마스크다. int로 하면 음수에서 %가 음수를 주고 오버플로도 UB라서 성립하지 않는다. 그래서 모범답안이 _Static_assert로 거듭제곱임을 못박는다.

Q: `head`가 `uint32_t` 최대를 넘어 0으로 돌아가면 링버퍼가 깨지는가?
A: 안 깨진다. 2^32가 8192의 배수라서 head & MASK의 순서가 연속으로 유지되고, 살아 있는 개수 head - tail도 부호 없는 뺄셈이라 wrap을 건너 올바른 값이 나온다. 조건은 두 값이 모두 unsigned이고 실제 개수가 CAP 이하라는 것이다.

Q: RSSI 같은 음수를 `uint64_t` 워드에 pack할 때 캐스트를 두 번 하는 이유는?
A: (uint64_t)(int16_t)(-97) 처럼 바로 넓히면 부호 확장 때문에 상위 48비트가 전부 1이 되어 같은 워드의 timestamp 필드를 덮어쓴다. (int16_t)로 폭을 정하고 (uint16_t)로 부호 없는 16비트만 꺼낸 다음 넓혀야 하위 16비트만 차지한다. 되돌릴 때는 반대로 (int16_t)(uint16_t)(word & 0xFFFF)다.

Q: 두 값을 따로 두지 않고 한 워드에 pack하면 동시성 측면에서 무엇을 얻는가?
A: 64비트 원자적 store 한 번으로 두 값이 함께 publish되므로, reader가 "새 rssi + 옛 timestamp" 같은 섞인 쌍을 볼 방법이 없다. 따로 두면 두 번의 store 사이에 reader가 끼어들 수 있다. 값이 한 워드에 안 들어갈 만큼 커지면 이 트릭이 끝나고 seqlock이나 mutex 스냅샷으로 넘어간다.

Q: 엔디안은 언제 신경 써야 하는가?
A: 정수를 바이트 단위로 꺼내거나 바이트에서 조립할 때만이다. 네트워크 패킷, 파일 포맷, 레지스터 덤프가 그 경우다. 프로그램 안에서 uint32_t로 계산만 하는 동안은 기계의 엔디안이 보이지 않는다. 해법은 be32()처럼 시프트와 OR로 순서를 코드에 명시하는 것이다.

Q: 문제들이 히스토리를 `malloc` 대신 `static` 고정 배열로 잡는 이유 세 가지는?
A: 첫째, 최악 메모리 사용량이 링크 시점에 확정되어 런타임에 부족해질 수 없다. 둘째, 수명이 프로그램 전체라 sampler와 reader 스레드가 함께 봐도 안전하고 해제 순서를 고민할 일이 없다. 셋째, free가 없으니 use-after-free가 원천적으로 생기지 않는다. 대가는 항상 최악치만큼 메모리를 점유한다는 것이다.

Q: `volatile`은 무엇을 해 주고 무엇을 해 주지 않는가?
A: 해 주는 것은 하나다. 컴파일러가 그 변수의 읽기/쓰기를 최적화로 없애거나 합치지 못하게 막는다. 해 주지 않는 것은 원자성, 메모리 순서, 스레드 간 가시성 전부다. 그래서 스레드 공유 플래그에는 _Atomic과 memory_order를 쓴다. 자세한 내용은 B6에서 다룬다.
```

## 9. 요약 카드

- 전역/static과 힙은 스레드가 **공유**한다. 스택은 스레드마다 **따로**다.
- 위험한 것은 지역 주소를 **반환**하거나 **다른 스레드에 남기는** 것. 호출 중에 쓰는 건 안전하다.
- 필드는 자기 정렬의 배수 위치에, **구조체 크기는 최대 정렬의 배수**. 그래서 `{u64, float}` = 16.
- 패딩 바이트 내용은 불확정 → 구조체를 `memcmp`로 비교하지 않는다.
- 예산 공식은 **샘플레이트 × 보관 시간 × 항목 크기**, 그다음 2의 거듭제곱으로 올림.
- `x & (N-1) == x % N` — N이 2의 거듭제곱, x가 unsigned일 때. `_Static_assert`로 박아 둔다.
- `head`/`tail`은 free-running `uint32_t`. `head - tail`이 개수이고 wrap에 안전하다.
- set `|=`, clear `&= ~`, toggle `^=`, test `& ... != 0`. 플래그는 `1u << n`, 64비트는 `(uint64_t)1 << n`.
- 음수 pack은 `(uint64_t)(uint16_t)(int16_t)v`. 엔디안은 바이트로 내리거나 올릴 때만.
- `volatile`은 최적화 억제까지만. 이전: [A1. C 기초 다시 세우기](A1_c_refresher.md)
