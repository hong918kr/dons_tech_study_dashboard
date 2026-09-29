# A1. C 기초 다시 세우기 — 포인터·구조체·함수 포인터

> **이 노트를 읽고 나면**
> - `int f(float *out)` 모양의 API를 보고 "반환값은 성공/실패, 데이터는 `*out`으로" 를 바로 읽어낸다
> - 구조체를 값으로 넘길 때와 포인터로 넘길 때 무엇이 복사되는지 말할 수 있다
> - `(out, max, *found)` 3인자 관용구를 직접 구현하고, 버퍼 넘침 없이 안전하게 채운다
> **선행**: 없음 (이 노트가 첫 번째다)
> **이 개념을 쓰는 문제**: 10문제 전부. 특히 [01_gps_fix_cache](../01_gps_fix_cache/question_note) 의 `gps_get_last_fix(struct GpsFix *out)`, [09_wifi_scan_snapshot](../09_wifi_scan_snapshot/question_note) 의 `wifi_scan(out, max, *found)`, [06_config_publish_rollback](../06_config_publish_rollback/question_note) 의 포인터 교체.

---

## 1. 왜 이게 필요한가

10개 문제의 API가 전부 같은 모양이다. 예를 들어 [01_gps_fix_cache/gps_cache.h](../01_gps_fix_cache/gps_cache.h) 는 이렇게 생겼다.

```c
int  gps_get_last_fix(struct GpsFix *out);   /* 0 = 채웠다, -1 = 아직 없다 */
int  gps_fix_at(uint64_t t, struct GpsFix *out);
```

여기서 막히는 지점은 동시성이 아니다. **함수가 값을 "돌려주는" 방법이 두 가지**라는 것부터 손에 안 붙어 있으면,
`out`이 무엇이고 누가 그 메모리를 소유하는지, 실패했을 때 `*out`을 건드려도 되는지를 매번 헷갈린다.

[09_wifi_scan_snapshot/wifi.h](../09_wifi_scan_snapshot/wifi.h) 의 벤더 함수는 한 발 더 나간다.

```c
int wifi_scan(struct ApInfo *out, size_t max, size_t *found);
```

`*found`가 `max`보다 클 수 있다. 즉 "본 개수"와 "쓴 개수"가 다르다. 이걸 구분 못 하면 버퍼 밖을 쓴다.
C의 배열이 함수로 넘어갈 때 크기를 잃어버린다는 사실이 이 3인자 관용구의 존재 이유인데,
그 이유를 모르면 관용구가 그냥 외울 것으로만 보인다.

이 노트는 **이 10문제가 실제로 요구하는 C만** 다룬다. 그 이상은 없다.

## 2. 그림으로 먼저

변수는 "이름 붙은 메모리 칸"이고, 포인터는 "그 칸의 주소를 담은 또 하나의 칸"이다.

```svg
<svg viewBox="0 0 560 200" role="img" aria-label="포인터는 주소를 담은 변수">
  <text class="lbl" x="10" y="20">메모리 (주소는 예시)</text>

  <rect class="fill-soft" x="60" y="40" width="150" height="46" rx="6"/>
  <text x="135" y="68" text-anchor="middle">42</text>
  <text class="lbl" x="135" y="102" text-anchor="middle">int x</text>
  <text class="lbl" x="135" y="34" text-anchor="middle">0x7ffe_1000</text>

  <rect class="box" x="330" y="40" width="180" height="46" rx="6"/>
  <text x="420" y="68" text-anchor="middle">0x7ffe_1000</text>
  <text class="lbl" x="420" y="102" text-anchor="middle">int *p</text>
  <text class="lbl" x="420" y="34" text-anchor="middle">0x7ffe_1008</text>

  <line class="accent" x1="330" y1="63" x2="222" y2="63"/>
  <polygon class="accent" points="222,63 232,58 232,68"/>
  <text class="lbl" x="276" y="55" text-anchor="middle">*p</text>

  <text class="lbl" x="135" y="150" text-anchor="middle">&amp;x = 0x7ffe_1000 · x = 42</text>
  <text class="lbl" x="420" y="150" text-anchor="middle">p = 0x7ffe_1000 · *p = 42</text>
  <text class="lbl" x="10" y="190">&amp; = "주소를 달라"   ·   * = "그 주소가 가리키는 칸"</text>
</svg>
```

`&`는 칸 → 주소, `*`는 주소 → 칸이다. 서로 반대 방향이고, `*&x`는 다시 `x`다.

out-파라미터는 이 그림을 함수 경계에 적용한 것이다. 호출자가 자기 칸의 주소를 주고, 함수가 그 칸에 쓴다.

```
호출자 (application thread)                 피호출자 (gps_get_last_fix)

  struct GpsFix f;   ← 40바이트 칸을 스택에 잡는다
        │  &f  (주소 8바이트만 넘어간다)
        ▼
  rc = gps_get_last_fix(&f) ───────────────►  out = 그 주소
        ▲                                        │
        │  rc: 0 = 채웠다 / -1 = 못 채웠다   ◄────┘  *out = <최신 fix>
  if (rc == 0) use(f);                            (호출자 칸에 직접 쓴다)
```

핵심: **함수는 호출자의 메모리를 빌려 쓴다. 메모리 소유자는 끝까지 호출자다.**
그래서 `gps_get_last_fix`는 malloc을 하지 않고, 호출자가 free할 것도 없다.

## 3. 개념 (용어를 하나씩)

### 주소와 값

**주소(address)** = 메모리 칸에 붙은 번호. 64비트 기계에서 8바이트 정수다. CPU는 "이름"을 모르고
번호로만 메모리를 읽고 쓴다. `&x`는 컴파일러가 x에 배정한 번호를 꺼내는 연산이다.

**포인터(pointer)** = 주소를 담는 변수. 타입이 붙는 이유는 `*p`로 읽을 때
**몇 바이트를 읽어서 어떻게 해석할지** 알아야 하기 때문이다. `int *`와 `double *`는 같은
8바이트를 담지만 `*p`의 뜻이 다르다.

**NULL** = "아무 곳도 가리키지 않는다"는 약속된 값. `*NULL`은 크래시(정확히는 UB)다. 그래서
out-파라미터를 받는 함수는 거의 항상 첫 줄에서 `if (out == NULL) return -1;` 을 한다 —
[01_gps_fix_cache/gps_cache_solution.c](../01_gps_fix_cache/gps_cache_solution.c) 의 `gps_get_last_fix`가 그렇게 시작한다.

### out-파라미터: 값을 두 개 돌려주는 C의 방법

C 함수는 값을 **하나만** 반환한다. 그런데 센서 API는 항상 두 가지를 알려줘야 한다 —
"성공했나?" 와 "값이 뭔가?". 그래서 관용구가 굳었다: **반환값 = 상태(0 / -1), 데이터 = `*out`.**

규칙 세 개가 따라온다.
- 실패하면 `*out`을 **건드리지 않는다**. 호출자의 이전 값이 살아 있어야 한다.
- `out`이 NULL인지 먼저 본다.
- 성공/실패를 호출자가 **반드시** 검사한다. 안 하면 쓰레기 값을 쓴다.

`wifi_scan`의 주석이 이 규칙을 명시한다: `-1`이면 `out`과 `*found`는 unspecified — 읽지 말라고.

### 구조체: 값이냐 포인터냐

**구조체(struct)** = 여러 필드를 한 덩어리로 묶은 타입. C에서 구조체는 **값 타입**이다.
대입하면 전체가 복사되고, 함수 인자로 넘기면 전체가 복사되고, 반환해도 전체가 복사된다.
그래서 `struct GpsFix gps_wait_for_fix(void)` 가 가능하다 — 40바이트를 통째로 복사해 돌려준다.
(ABI 수준에서는 레지스터 몇 개나 호출자가 미리 잡아 둔 공간을 쓰지만, C에서 볼 때는 그냥 값이다.)

값으로 넘기면 함수 안의 수정이 호출자에게 **안 보인다**. 포인터로 넘기면 보인다.
40바이트면 값 복사도 싸지만, [05_frame_latest_and_replay](../05_frame_latest_and_replay/question_note) 의 프레임은 4 KiB(실제 센서는 8 MB)라서
값으로 넘기는 순간 성능이 죽는다. 판단 기준은 **크기 + 수정 필요 여부**다.

### 구조체 대입은 "원자적"이 아니다 (B 트랙 예고)

`b = a;` 한 줄이 소스에서는 한 문장이지만, 기계어에서는 **여러 번의 load/store**다.
40바이트면 8바이트씩 5번쯤 옮긴다. 그 사이에 다른 스레드가 `a`를 고치면
`b`는 앞부분은 옛 값 뒷부분은 새 값인 **찢어진(torn)** 구조체가 된다.
`01_gps_fix_cache`의 지문이 이걸 정확히 지적한다 — "지난 초의 lat과 이번 초의 lon"이 섞이면
그 좌표는 다른 county에 있고 geofence 경보가 잘못 울린다. 막는 방법이 B 트랙(seqlock, mutex 스냅샷,
더블 버퍼)의 주제다. 지금은 **한 줄 대입이 한 번의 동작이 아니다**만 기억한다.

### 배열과 포인터

배열은 포인터가 아니다. `int a[5]`는 20바이트 덩어리고, `sizeof a`는 20이다.
그런데 **함수 인자로 쓰면 첫 원소의 주소로 붕괴(decay)한다.** 크기 정보가 사라진다.

```c
static void takes_array(int a[5])   /* 컴파일러가 int *a 로 바꿔 버린다 */
{
    printf("%zu\n", sizeof a);      /* 8. 20 이 아니다 */
}
```

clang이 `-Wsizeof-array-argument` 경고로 잡아준다: "sizeof on array function parameter will
return size of `int *` instead of `int[5]`". 그래서 **크기를 별도 인자로 넘겨야 한다.**
여기서 3인자 관용구가 나온다.

### `(out, max, *found)` 관용구

```c
int wifi_scan(struct ApInfo *out, size_t max, size_t *found);
```

- `out` = 어디에 쓸지 (호출자 소유)
- `max` = 쓸 수 있는 최대 개수 (배열이 잃어버린 크기를 되돌려 준다)
- `*found` = **실제로 본 개수**. `max`보다 클 수 있다.

쓴 개수는 `min(*found, max)`다. 호출자는 "라디오가 40개를 봤는데 내 버퍼에는 4개만 들어왔다"를
알아야 버퍼를 키울지 판단할 수 있다. 그래서 두 값을 따로 준다.

### `const`는 위치가 의미다

읽는 순서는 **오른쪽에서 왼쪽**이다.

| 선언 | 읽는 법 | `*p = 'x'` | `p++` |
|---|---|---|---|
| `const char *p` | p는 const char를 가리키는 포인터 | 금지 | 허용 |
| `char *const p` | p는 char를 가리키는 const 포인터 | 허용 | 금지 |
| `const char *const p` | 둘 다 const | 금지 | 금지 |

`const char *p`와 `char const *p`는 **완전히 같다**. 별표 왼쪽의 const는 대상에 붙는다.
실전에서 `const`는 문서다. `int wifi_lookup(const uint8_t bssid[6], struct ApEntry *out)` 를 보면
"bssid는 읽기만, out은 쓴다"가 시그니처에서 읽힌다. 그리고 실수로 쓰면 컴파일이 깨진다.

### `static`의 세 가지 의미

같은 키워드가 위치에 따라 다른 뜻이다. 이게 초보자를 가장 많이 헷갈리게 한다.

| 위치 | 의미 | 예 |
|---|---|---|
| 파일 수준 변수 | 내부 링키지 = **이 .c 파일 밖에서 안 보인다** | `static _Atomic uint32_t g_seq;` |
| 파일 수준 함수 | 내부 링키지 = 이 파일 전용 헬퍼 | `static double segment_m(...)` |
| 함수 안 변수 | **수명이 프로그램 전체** (호출 간에 값이 유지) | `static int calls = 0;` |

문제들의 전역 상태가 전부 `static`인 이유: `gps_cache.c`와 `rssi_window.c`를 같은 하네스에 링크할 때
`g_seq` 같은 흔한 이름이 충돌하면 안 된다. `static`은 이름을 파일 안에 가둔다.
또 초기값 없는 `static`은 **0으로 보장 초기화**된다 — `g_seq = 0`, `g_running = false`를 따로 안 써도 된다.

### 함수 포인터와 ops 구조체

**함수 포인터** = 함수의 주소를 담는 변수. 선언이 읽기 어렵지만 규칙은 하나다.

```c
uint64_t (*now_us)(void);
```

"`now_us`는 포인터인데(`*now_us`), 그 대상은 `void`를 받아 `uint64_t`를 돌려주는 함수"로 읽는다.
괄호를 빼면 `uint64_t *now_us(void)` = 포인터를 반환하는 함수가 되어 전혀 다른 뜻이다.

왜 필요한가: 벤더 함수를 **테스트에서 갈아끼우기** 위해서다. 함수 포인터 여러 개를 구조체로 묶으면
그게 ops 구조체 = 의존성 주입(dependency injection)의 가장 단순한 형태다.

```
                                                    ┌──────────────┐  실제 장치
                                                ┌──►│ gps_now_us() │
  ┌───────────┐      ┌──────────────────────┐   │   └──────────────┘
  │  내 코드   │─────►│  struct clock_ops    │───┤
  │ ops->     │      │  uint64_t (*now_us)  │   │   ┌──────────────┐  테스트
  │  now_us() │      └──────────────────────┘   └──►│ fake_now()   │
  └───────────┘         ops 한 줄만 갈아끼운다       └──────────────┘
```

```c
struct clock_ops { uint64_t (*now_us)(void); };
static uint64_t fake_now(void) { return 1234567; }
static const struct clock_ops g_fake_clock = { .now_us = fake_now };
static uint64_t stamp(const struct clock_ops *ops) { return ops->now_us(); }
/* stamp(&g_fake_clock) -> 1234567.  내 코드는 어느 쪽인지 모른다. */
```

### 헤더와 소스

**선언(declaration)** = "이런 이름이 이런 타입으로 있다"(`.h`).
**정의(definition)** = 실제 코드와 메모리(`.c`).
`.h`는 여러 `.c`에 텍스트로 복사되므로, 두 번 펼쳐져도 깨지지 않게 **include 가드**를 쓴다.
`01_gps_fix_cache`의 모든 `.h`가 이 모양이고, 가드 이름은 파일명을 대문자로 바꾼 것이다.

```c
#ifndef GPS_CACHE_H
#define GPS_CACHE_H
/* ... 선언들 ... */
#endif /* GPS_CACHE_H */
```

### 문자열과 `snprintf`

C 문자열은 **`'\0'`으로 끝나는 char 배열**이다. 길이를 따로 저장하지 않는다.
`char ssid[33]`은 "최대 32자 + NUL"이다.

`snprintf`는 항상 NUL로 끝내고, 반환값은 **잘리지 않았다면 필요했을 길이**다.
그래서 `n >= dstsz`이면 잘린 것이다. `strcpy`는 크기를 모르므로 절대 쓰지 않는다.
고정 크기 배열끼리의 복사는 `memcpy` + NUL 강제가 더 정확하다 —
[09_wifi_scan_snapshot/ap_table_solution.c](../09_wifi_scan_snapshot/ap_table_solution.c) 가 그렇게 한다.

```c
memcpy(s->e.ssid, a->ssid, sizeof s->e.ssid);   /* 33바이트 통째로 */
s->e.ssid[sizeof s->e.ssid - 1] = '\0';         /* 벤더를 믿지 않는다 */
```

## 4. 코드로 보기

out-파라미터의 세 가지 규칙을 한 파일에서 본다.

```c
#include <stdio.h>

static int sensor_read(float *out)
{
    static int calls = 0;      /* static: 호출 간에 값이 유지된다 */
    calls++;
    if (calls == 2)
        return -1;             /* 실패: *out 을 건드리지 않는다  <- 이 줄이 핵심 */
    *out = 12.5f * (float)calls;
    return 0;
}

int main(void)
{
    float v = -1.0f;
    for (int i = 0; i < 3; i++) {
        if (sensor_read(&v) == 0) printf("read ok   v=%.1f\n", (double)v);
        else                      printf("read fail v=%.1f (그대로)\n", (double)v);
    }
    return 0;
}
```

돌려본 출력. 두 번째 호출이 실패했지만 `v`는 `12.5`로 남아 있다.

```
read ok   v=12.5
read fail v=12.5 (그대로)
read ok   v=37.5
```

- `*out = ...` 이 없으면? 호출자의 `v`가 영원히 `-1.0`이다. 반환값만 보고 값을 안 쓴 것.
- `return -1` 전에 `*out = 0`을 넣으면? 실패 시 호출자의 마지막 좋은 값이 0으로 덮인다.
  `gps_get_last_fix`의 계약이 "실패 시 `*out` untouched"인 이유가 이것이다.
- `&v`에서 `&`를 빼면? `float`을 `float *`로 넘기게 되어 컴파일 에러. (에러가 나는 게 다행이다.)
- `static`을 빼면? `calls`가 매번 0으로 새로 시작해 절대 2가 안 되고, 실패 경로가 안 돌아간다.

구조체는 값이냐 포인터냐로 결과가 갈린다.

```c
struct GpsFix { int status; double lat, lon; float hdop; uint64_t timestamp; };

static struct GpsFix make_fix(void)
{
    struct GpsFix f = { 2, 37.5, -122.5, 1.0f, 1000 };
    return f;                          /* 40바이트를 값으로 돌려준다 */
}

static void bump_by_value(struct GpsFix f) { f.lat += 1.0; }   /* 사본만 바뀐다 */
static void bump_by_ptr(struct GpsFix *f)  { f->lat += 1.0; }  /* 원본이 바뀐다 */
```

`a = make_fix()` 다음 `bump_by_value(a)` 후 `a.lat`은 여전히 `37.5`, `bump_by_ptr(&a)` 후에는 `38.5`.
돌려본 출력이 정확히 그랬다. `f->lat`은 `(*f).lat`의 줄임일 뿐이다.

## 5. 단계별로 만들어 보기

`09_wifi_scan_snapshot` 스타일의 스캔 함수를 세 번에 걸쳐 고친다.

**v0 — 틀렸다. 크기를 안 넘긴다.**

```c
static void scan_v0(int *out)
{
    static const int seen[7] = {10,20,30,40,50,60,70};
    for (int i = 0; i < 7; i++) out[i] = seen[i];   /* out 이 몇 칸인지 모른다 */
}
/* 호출: int buf[4]; scan_v0(buf);  -> buf[4..6] 은 남의 스택. 조용히 망가진다. */
```

무엇이 틀렸나: 배열이 함수로 넘어오면서 크기를 잃었는데, 그 크기를 아무도 알려주지 않았다.
컴파일도 되고 경고도 없고 대부분 크래시도 안 난다. 며칠 뒤 엉뚱한 변수가 깨진다. 최악의 버그다.

**v1 — 크기를 받는다. 아직 부족하다.**

```c
static size_t scan_v1(int *out, size_t max)
{
    static const int seen[7] = {10,20,30,40,50,60,70};
    size_t w = (7u < max) ? 7u : max;
    for (size_t i = 0; i < w; i++) out[i] = seen[i];
    return w;                        /* 쓴 개수만 알려준다 */
}
```

안전해졌다. 하지만 호출자는 "4개 받았다"만 알고 **3개를 놓쳤다는 사실을 모른다.**
버퍼를 키워야 하는지 판단할 근거가 없다.

**v2 — 본 개수와 쓴 개수를 분리한다. 실제 API 모양.**

```c
static int scan_v2(int *out, size_t max, size_t *found)
{
    static const int seen[7] = {10,20,30,40,50,60,70};
    if (out == NULL || found == NULL)
        return -1;
    *found = 7;                                  /* 본 개수 (max 보다 클 수 있다) */
    size_t w = (*found < max) ? *found : max;    /* 쓴 개수 */
    for (size_t i = 0; i < w; i++) out[i] = seen[i];
    return 0;
}

/* 호출 쪽 */
int buf[4];
size_t found = 0;
if (scan_v2(buf, 4, &found) == 0) {
    size_t n = (found < 4) ? found : 4;          /* 절대 found 로 루프 돌지 말 것 */
    printf("found=%zu written=%zu first=%d last=%d\n", found, n, buf[0], buf[n-1]);
}
```

출력: `found=7 written=4 first=10 last=40`. 가장 위험한 줄은 호출자의 `n = min(found, 4)` 다.
이걸 빼고 `for (i = 0; i < found; i++)` 로 읽으면 v0와 똑같이 버퍼 밖을 읽는다.
`wifi.h`가 "Never index past that"이라고 대문자로 적어 둔 이유다.

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
|---|---|---|---|
| `f(&v)` 에서 `&`를 빼먹음 | 컴파일 에러 (운이 좋다) | `float`과 `float *`는 다른 타입 | 에러 메시지의 타입 두 개를 읽고 `&` 추가 |
| 실패 경로에서 `*out`을 0으로 덮음 | 호출자의 마지막 좋은 값이 사라짐 | 계약이 "실패 시 untouched" | `return -1` 전에 `*out`을 건드리지 않는다 |
| 반환값을 검사하지 않음 | 초기화 안 된 스택 값을 좌표로 사용 | 실패 시 `*out`은 그대로 = 쓰레기 | `if (f(&v) != 0) return;` 을 항상 쓴다 |
| `for (i = 0; i < *found; i++)` | 버퍼 밖 읽기/쓰기, 랜덤 크래시 | `*found`는 본 개수, 버퍼 크기가 아니다 | `min(*found, max)`로 루프 |
| 함수 안에서 `sizeof arr` | 항상 8 (포인터 크기) | 배열 인자는 포인터로 decay | 크기를 별도 인자로 받는다 |
| `static`을 빼고 전역 선언 | 다른 `.c`와 링크 시 중복 정의 또는 조용한 공유 | 기본이 외부 링키지 | 파일 전용 상태에는 전부 `static` |
| `uint64_t (*f)(void)` 괄호 빼먹음 | "포인터 반환 함수"로 해석, 대입 에러 | `*`가 반환 타입에 붙어 버린다 | 이름을 항상 `(*name)`으로 감싼다 |
| `strcpy(ssid, src)` | 33바이트 배열에 34자 이상 → 스택 파괴 | 목적지 크기를 모르는 함수 | `snprintf(dst, sizeof dst, "%s", src)` |
| 헤더에 include 가드 없음 | "redefinition of struct" 컴파일 에러 | 헤더가 두 번 펼쳐진다 | `#ifndef X_H / #define X_H / #endif` |

## 7. 손으로 확인하기

먼저 실제 문제의 API 모양을 눈으로 익힌다.

```sh
sed -n '1,40p' 01_gps_fix_cache/gps_cache.h        # out-파라미터 계약이 주석에 다 있다
grep -n "out == NULL" 01_gps_fix_cache/gps_cache_solution.c
grep -n "found" 09_wifi_scan_snapshot/wifi.h       # 본 개수 vs 쓴 개수
grep -n "wifi_scan(" 09_wifi_scan_snapshot/main.c 09_wifi_scan_snapshot/ap_table_solution.c
```

배열 decay를 직접 본다. 스크래치 파일을 만들어:

```sh
cat > /tmp/decay.c <<'END'
#include <stdio.h>
static void f(int a[5]) { printf("in  f: %zu\n", sizeof a); }
int main(void) { int a[5]; printf("in main: %zu\n", sizeof a); f(a); return 0; }
END
cc -std=c11 -Wall -Wextra -o /tmp/decay /tmp/decay.c && /tmp/decay
```

`in main: 20` / `in f: 8` 이 나오고, 컴파일러가 `-Wsizeof-array-argument` 경고를 띄운다.

`struct GpsFix`가 왜 정확히 40바이트인지는 [A2. 메모리와 비트](A2_memory_and_bits.md) 의 §4에서 직접 재 본다.

## 8. 자가 점검

```check
Q: `int gps_get_last_fix(struct GpsFix *out)` 에서 반환값과 `*out`은 각각 무엇을 뜻하는가?
A: 반환값은 성공/실패다. 0이면 out을 채웠고 -1이면 아직 유효한 fix가 없다. 데이터는 전부 *out으로 나간다. C 함수가 값을 하나만 반환하므로 "상태"와 "값"을 이렇게 나눈다. 실패했을 때 *out은 건드리지 않는 것이 계약이다.

Q: `struct GpsFix gps_wait_for_fix(void)` 처럼 40바이트 구조체를 값으로 반환할 수 있는 이유는?
A: C에서 구조체는 값 타입이라 대입·인자 전달·반환이 모두 전체 복사로 정의되어 있다. 컴파일러가 ABI에 따라 레지스터나 호출자가 미리 잡아 둔 공간으로 옮겨 준다. 소스 수준에서는 int를 반환하는 것과 똑같이 취급하면 된다.

Q: `b = a;` 가 한 줄인데 왜 "원자적"이 아닌가?
A: 40바이트를 한 번에 옮기는 명령이 없어서 기계어로는 8바이트씩 여러 번의 load/store가 된다. 그 사이에 다른 스레드가 a를 고치면 b는 앞부분 옛 값 + 뒷부분 새 값이 섞인 torn 구조체가 된다. 01번 문제의 "지난 초 lat + 이번 초 lon" 버그가 바로 이것이고, 해결책은 B 트랙의 주제다.

Q: `wifi_scan(out, max, &found)` 가 성공했을 때 `out`을 몇 개까지 읽어도 되는가?
A: min(found, max)개다. found는 라디오가 "본" AP 개수여서 max보다 클 수 있고, 함수는 max개까지만 썼다. found로 루프를 돌면 버퍼 밖을 읽는다. 두 값을 따로 주는 이유는 호출자가 버퍼를 키울지 판단할 수 있게 하는 것이다.

Q: `const char *p` 와 `char *const p` 의 차이는?
A: 앞은 대상이 const라서 *p = 'x'가 금지되고 p++은 된다. 뒤는 포인터가 const라서 *p = 'x'는 되고 p++이 금지된다. 오른쪽에서 왼쪽으로 읽으면 구분된다. 별표 왼쪽의 const는 항상 가리키는 대상에 붙는다.

Q: 문제들의 전역 변수가 전부 `static`인 이유 두 가지를 말해 보라.
A: 첫째, 내부 링키지가 되어 이름이 그 .c 파일 안에 갇힌다. g_seq 같은 흔한 이름이 다른 파일과 충돌하지 않는다. 둘째, 초기값을 안 써도 0으로 보장 초기화된다. g_running = false, g_seq = 0을 따로 적을 필요가 없다.

Q: 벤더 함수를 테스트에서 가짜로 바꿔 끼우려면 무엇을 쓰는가?
A: 함수 포인터를 필드로 가진 ops 구조체를 만들고, 내 코드는 항상 ops->now_us() 처럼 그 구조체를 통해 호출한다. 실제 환경에서는 진짜 함수를, 테스트에서는 fake 함수를 가리키는 ops를 주입한다. 의존성 주입의 가장 단순한 C 구현이다.

Q: `snprintf(dst, dstsz, "%s", src)` 가 잘렸는지 어떻게 아는가?
A: 반환값이 "잘리지 않았다면 필요했을 길이"다. 따라서 n < 0(인코딩 에러)이거나 (size_t)n >= dstsz면 잘린 것이다. snprintf는 어떤 경우에도 dst를 NUL로 끝내 주므로 문자열로 쓰는 것 자체는 안전하다.
```

## 9. 요약 카드

- `&` = 칸에서 주소로, `*` = 주소에서 칸으로. 서로 역연산이다.
- out-파라미터 계약: **반환값 = 상태, `*out` = 데이터, 실패 시 `*out` 손대지 않음.**
- 구조체는 값 타입이다. 대입·전달·반환이 모두 전체 복사. 크면 포인터로.
- 한 줄 구조체 대입은 기계어 여러 개다 → 동시성에서 찢어진다(B 트랙).
- 배열은 함수 인자로 가면 크기를 잃는다 → `(out, max, *found)`.
- 호출자는 반드시 `min(*found, max)`까지만 읽는다.
- `const`는 오른쪽에서 왼쪽으로 읽는다. 별표 왼쪽 const는 대상에 붙는다.
- `static` 세 가지: 파일 스코프 / 지역 수명 / 내부 링키지. 전역 상태는 전부 `static`.
- 함수 포인터는 `(*name)`으로 감싼다. 여러 개 묶으면 ops 구조체 = 의존성 주입.
- 헤더에는 선언 + include 가드, 소스에는 정의. `strcpy` 대신 `snprintf`.
- 다음: [A2. 메모리와 비트 — 스택·힙·정렬·비트 연산](A2_memory_and_bits.md)
