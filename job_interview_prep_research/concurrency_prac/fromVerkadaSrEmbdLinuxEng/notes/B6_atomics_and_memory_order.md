# B6. 원자 연산과 메모리 순서 — volatile로는 안 되는 이유

> **이 노트를 읽고 나면**:
> - 공유 데이터의 문제를 **원자성 / 가시성 / 순서** 세 가지로 쪼개서 말할 수 있다
> - `_Atomic` 과 `release`/`acquire` 를 짝으로 써서 "데이터 먼저, 깃발 나중"을 안전하게 만들 수 있다
> - 왜 `volatile`이 스레드 동기화에 쓰이면 안 되는지, 실제로 무엇이 깨지는지 예를 들어 설명할 수 있다
>
> **선행**: [B3. 레이스와 임계 구역](B3_race_and_critical_section.md), [B4. mutex](B4_mutex.md)
> **이 개념을 쓰는 문제**: [02_modem_rssi_window](../02_modem_rssi_window/question_note) Part 1 (값+시각을 `_Atomic uint64_t` 하나에) · [01_gps_fix_cache](../01_gps_fix_cache/question_note) Part 1 (seqlock의 fence) · [10_heartbeat_watchdog](../10_heartbeat_watchdog/question_note) Part 1 (writer 다수, wait-free)

---

## 1. 왜 이게 필요한가

`02_modem_rssi_window` 의 getter 시그니처는 이렇다.

```c
int rssi_get_latest(int *dbm, uint64_t *age_us);
```

값과 **그 값의 나이**를 같이 준다. 이 둘은 반드시 **같은 샘플**이어야 한다.
`-113 dBm`(사실상 끊긴 상태)이 "0 ms 전 값"으로 보이면 failover 로직이 죽은 모뎀을 믿는다.
그런데 `static int g_rssi; static uint64_t g_ts;` 두 변수로 두면, 샘플러가 두 줄을 쓰는 사이에
reader가 끼어들어 **옛 값 + 새 시각** 조합을 가져간다. mutex를 쓰면 막히지만, 문제 지문은
getter가 블로킹하지 않을 것을 요구한다. 그래서 원자 연산이 필요하다.

여기서 초보자가 거의 항상 하는 첫 시도가 `volatile`이다. 그게 틀렸다는 사실이 이 노트의 절반이다.

## 2. 그림으로 먼저

공유 데이터의 문제는 하나가 아니라 **셋**이다. 섞어서 생각하면 정리되지 않는다.

```svg
<svg viewBox="0 0 700 290" role="img" aria-label="공유 데이터의 세 가지 문제">
  <text x="350" y="20" text-anchor="middle">공유 데이터의 문제는 셋이다 — 따로 봐야 한다</text>
  <rect class="box" x="15" y="38" width="205" height="238" rx="10"/>
  <text x="117" y="63" text-anchor="middle">① 원자성</text>
  <text class="lbl" x="117" y="82" text-anchor="middle">한 덩어리로 끝나는가?</text>
  <rect class="fill-soft" x="33" y="96" width="169" height="32" rx="6"/>
  <text class="lbl" x="117" y="117" text-anchor="middle">counter++ = load, +1, store</text>
  <line class="accent" x1="58" y1="144" x2="58" y2="238"/><line class="muted" x1="176" y1="144" x2="176" y2="238"/>
  <text class="lbl" x="58" y="158" text-anchor="middle">A</text><text class="lbl" x="176" y="158" text-anchor="middle">B</text>
  <text class="lbl" x="86" y="180">load 5</text><text class="lbl" x="116" y="200">load 5</text>
  <text class="lbl" x="86" y="222">store 6</text><text class="lbl" x="33" y="262">store 6 ← 증가가 사라짐</text>

  <rect class="box" x="245" y="38" width="205" height="238" rx="10"/>
  <text x="347" y="63" text-anchor="middle">② 가시성</text>
  <text class="lbl" x="347" y="82" text-anchor="middle">상대가 언제 보게 되나?</text>
  <rect class="fill-soft" x="263" y="96" width="169" height="32" rx="6"/>
  <text class="lbl" x="347" y="117" text-anchor="middle">컴파일러가 레지스터에 캐시</text>
  <text class="lbl" x="263" y="156">while (!flag) { }</text>
  <text class="lbl" x="263" y="180">→ flag를 한 번만 읽고</text>
  <text class="lbl" x="263" y="204">→ 영원히 도는 코드로 번역</text>
  <text class="lbl" x="263" y="240">store buffer / 캐시 지연도</text>
  <text class="lbl" x="263" y="262">같은 종류의 문제다</text>

  <rect class="box" x="475" y="38" width="210" height="238" rx="10"/>
  <text x="580" y="63" text-anchor="middle">③ 순서</text>
  <text class="lbl" x="580" y="82" text-anchor="middle">쓴 순서대로 보이나?</text>
  <rect class="fill-soft" x="493" y="96" width="172" height="32" rx="6"/>
  <text class="lbl" x="579" y="117" text-anchor="middle">data → flag 가 뒤집힌다</text>
  <text class="lbl" x="493" y="156">쓴 쪽:  data=42; flag=1;</text>
  <text class="lbl" x="493" y="184">본 쪽:  flag=1  (먼저 보임)</text>
  <text class="lbl" x="493" y="206">        data=쓰레기</text>
  <text class="lbl" x="493" y="240">컴파일러 재배치 + CPU 재배치</text>
  <text class="lbl" x="493" y="262">두 층이 각각 뒤집는다</text>
</svg>
```

## 3. 개념 (용어를 하나씩)

### 3.1 원자성 (atomicity)

**정의**: 어떤 연산이 다른 스레드 눈에 "다 됐거나 안 됐거나" 둘 중 하나로만 보이면 원자적이다.
중간 상태가 관측되지 않는다.

`counter++`는 원자적이지 **않다**. 기계어 세 개다 — 읽고, 더하고, 쓴다.
두 스레드가 각각 100만 번 증가시킨 실측값이다 (Apple M2, `cc -O2`).

```
volatile=1361601  atomic=2000000  (기대값 2000000, 잃어버린 증가 638399)
volatile=1201935  atomic=2000000  (기대값 2000000, 잃어버린 증가 798065)
```

`volatile int`는 절반 가까이 잃었다. 왜 그런지는 컴파일된 기계어에 그대로 있다.

```
	ldr	w12, [x9, _vol@PAGEOFF]     ; volatile: 읽고
	add	w12, w12, #1                ;           더하고
	str	w12, [x9, _vol@PAGEOFF]     ;           쓴다 — 3단계
	ldadd	w10, w12, [x11]             ; atomic_fetch_add: 명령어 하나
```

### 3.2 가시성 (visibility) 과 순서 (ordering)

**가시성**: 내가 쓴 값이 다른 스레드의 다음 읽기에 보이는지. 두 층에서 막힌다 —
**컴파일러**(반복문 안에서 안 변할 변수는 레지스터로 올린다)와 **하드웨어**(store buffer에 머문다).

**순서**: 내가 A, B 순서로 썼을 때 남이 A, B 순서로 보는지. 원자성과 가시성을 다 해결해도
이건 남는다. `atomic`이라고 쓰면 ①은 해결되지만 ③은 **따로 지정해야** 한다.

### 3.3 data race — C 표준이 말하는 것

**정의** (C11 §5.1.2.4): 두 스레드가 같은 메모리 위치에 접근하고, 적어도 하나가 쓰기이고,
둘 사이에 happens-before 관계가 없으면 **data race**이고, 그 프로그램의 동작은
**정의되지 않는다(UB)**.

"정의되지 않는다"는 "이상한 값이 나온다"보다 훨씬 나쁘다. 컴파일러는 data race가 없다고
**가정하고** 최적화한다. 그래서 값이 이상해지는 게 아니라 **코드가 사라지거나 무한 루프로
바뀐다**(§5.5에 실물이 있다). race를 없애는 방법은 표준이 정한 두 가지뿐이다 —
**락으로 막거나, atomic으로 선언하거나**. `volatile`은 그 목록에 없다.

### 3.4 `volatile`이 하는 일과 하지 않는 일

`volatile`이 보장하는 것은 하나다: **접근을 지우거나 합치거나 옮기지 말 것**(컴파일러에게).
`volatile` 접근끼리의 상대 순서는 컴파일러가 지킨다. 스레드 동기화에 못 쓰는 이유 3가지다.

1. **원자성이 없다.** `volatile v; v++;`는 여전히 `ldr / add / str` 3단계다. §3.1의 측정값이 증거다.
2. **CPU 재배치를 막지 못한다.** `volatile`은 배리어 명령을 하나도 내보내지 않는다. ARM에서는 `data=42; flag=1;`이 뒤집혀 보인다 — [B7 §5](B7_lockfree_patterns.md)에서 200회 중 38회 깨졌다.
3. **비-`volatile` 접근과 묶어주지 못한다.** 깃발만 `volatile`로 해도 payload 쪽 접근은 자유롭게 옮겨진다. 표준상 여전히 data race = UB이고 TSan도 race로 잡는다.

3번을 확인한 실제 TSan 출력이다 — 깃발을 `volatile int`로 두고 돌렸다.

```
WARNING: ThreadSanitizer: data race (pid=8670)
  Write of size 4 at 0x000104984000 by thread T2:
    #0 producer v0_volatile.c:12
  Previous read of size 4 at 0x000104984000 by thread T1:
    #0 consumer v0_volatile.c:16
  Location is global 'g_ready' at 0x000104984000
SUMMARY: ThreadSanitizer: data race v0_volatile.c:12 in producer
```

### 3.5 그럼 `volatile`은 어디에 쓰나

세 군데다. 전부 **한 스레드 안의** 문제라는 공통점이 있다.

| 용도 | 왜 `volatile`이 맞는가 |
| --- | --- |
| MMIO 레지스터 `volatile uint32_t *reg` | 하드웨어가 값을 바꾼다. 컴파일러가 읽기를 지우면 안 되고, 두 번 읽으라고 쓴 건 두 번 읽어야 한다 (clear-on-read 상태 레지스터) |
| 시그널 핸들러와 주고받는 깃발 `volatile sig_atomic_t` | 핸들러는 **같은 스레드**를 비동기로 가로챈다. 표준이 이 조합만 보장한다 |
| `setjmp`/`longjmp` 를 건너 살아남아야 하는 지역 변수 | 표준 요구사항. 레지스터에 있으면 `longjmp` 후 값이 불확정 |

**중요**: `volatile sig_atomic_t`는 "**같은 스레드**의 시그널 핸들러"에 대해서만 원자적이다.
스레드 사이에서는 아무것도 보장하지 않는다. 이름에 낚이지 말 것.
MMIO에도 배리어가 따로 필요하다 — 그래서 커널은 `readl()`/`writel()` 래퍼를 쓴다.

### 3.6 C11 `<stdatomic.h>` 기본

선언에 `_Atomic` 을 붙인다. 그러면 그 객체에 대한 접근은 **data race가 아니다**.
기본 연산은 여섯 개만 알면 문제를 풀 수 있다.

| 함수 | 하는 일 | 반환 |
| --- | --- | --- |
| `atomic_load(&a)` | 읽는다 | 읽은 값 |
| `atomic_store(&a, v)` | 쓴다 | 없음 |
| `atomic_fetch_add(&a, n)` | `a += n` 을 한 덩어리로 | **바뀌기 전** 값 |
| `atomic_fetch_sub(&a, n)` / `atomic_exchange(&a, v)` | `a -= n` / `a = v` 를 한 덩어리로 | **바뀌기 전** 값 / 옛 값 |
| `atomic_compare_exchange_weak(&a, &exp, v)` | `a == *exp` 이면 `a = v`, 아니면 `*exp = a` | 성공 여부 |

이름 뒤에 `_explicit`을 붙이면 `memory_order` 인자를 직접 준다.
**붙이지 않으면 `memory_order_seq_cst`** — 가장 강하고 비싼 기본값이다.

`atomic_store(&a, 1)` 는 seq_cst, `atomic_store_explicit(&a, 1, memory_order_release)` 는
내가 고르는 것이다. 처음에는 `_explicit` 없이 쓰는 게 맞다 — 느려서 문제가 된다는
**측정**이 있을 때만 완화한다.

### 3.7 lock-free — 크면 atomic이 아니다

`_Atomic`을 붙였다고 항상 기계어 한 개가 되는 건 아니다. 하드웨어에 그만한 명령이 없으면
컴파일러가 **몰래 락을 붙인다**. 실측값이다.

```
sizeof(struct GpsFix)      = 40
_Atomic float    lock-free = 1
_Atomic double   lock-free = 1
_Atomic uint64_t lock-free = 1
_Atomic GpsFix   lock-free = 0      ← 락이 숨어 있다
```

모범답안 `01_gps_fix_cache/gps_cache_solution.c` 파일 머리에 이 얘기가 그대로 있다.

```
 * struct GpsFix is 40 bytes.  There is no 40-byte atomic store, and a
 * `_Atomic struct GpsFix` would silently become a compiler-managed lock (check
 * atomic_is_lock_free), so the choice is a real lock or a versioning scheme.
```

규칙: **구조체에 `_Atomic`을 붙이지 말 것.** 64비트에 들어가면 하나로 packing하고,
안 들어가면 seqlock이나 mutex다 ([B7](B7_lockfree_patterns.md)).

## 4. 코드로 보기

### 4.1 가장 작은 예제 — 카운터

```c
static _Atomic uint32_t g_errors;

/* 통계 카운터. 지금 당장 정확한 값이 필요한 게 아니라 총합만 맞으면 된다. */
atomic_fetch_add_explicit(&g_errors, 1u, memory_order_relaxed);
```

`relaxed`인 이유: 이 카운터 값으로 다른 데이터의 유효성을 판단하지 않는다. 증가가
사라지지만 않으면 된다. `01`, `02`, `06` 세 모범답안 모두 에러 카운터는 `relaxed`다.
**`g_errors++`로 바꾸면?** §3.1처럼 증가가 사라진다.

### 4.2 CAS 루프 — "지금까지 본 최대값"

`fetch_add`로 못 하는 것이 있다. "기존 값보다 클 때만 갱신"은 읽고-비교하고-쓰는 한 덩어리가
필요하다. 그게 CAS(compare-and-swap)다.

```c
static _Atomic int g_peak = -128;

static void record_peak(int v)
{
    int cur = atomic_load_explicit(&g_peak, memory_order_relaxed);
    while (v > cur) {
        /* 성공하면 g_peak = v. 실패하면 cur 에 "진짜 현재값"이 들어온다. */
        if (atomic_compare_exchange_weak_explicit(
                &g_peak, &cur, v,
                memory_order_release,      /* 성공했을 때의 순서 */
                memory_order_relaxed))     /* 실패했을 때의 순서 */
            return;
        /* 실패 = 누가 먼저 바꿨거나(진짜 실패) spurious(가짜 실패).
         * 어느 쪽이든 갱신된 cur 로 다시 비교하면 된다. */
    }
}
```

- `&cur`는 **입출력**이다. 실패하면 CAS가 `cur`를 현재값으로 덮어쓴다 — 루프 안에서 다시 `atomic_load`할 필요가 없다. 순서 인자가 **둘**인 것도 특이하다 — 실패 순서에는 `release`/`acq_rel`를 쓸 수 없다 (C11은 성공 순서보다 강한 것도 금지했고, 그 제약은 C17에서 풀렸다).
- `weak`는 **값이 같아도 실패할 수 있다**(spurious failure). ARM·RISC-V 같은 LL/SC 기계에서 `ldxr`/`stxr` 짝으로 구현되는데, 그 사이에 인터럽트가 나거나 다른 코어가 같은 캐시 라인을 건드리면 `stxr`가 값과 무관하게 실패한다. 그래서 **루프를 돌 코드에서는 `weak`**, 한 번만 시도할 거면 `strong`(내부에 재시도 루프가 있어 이미 루프 안이면 이중 루프다).

**ABA 문제**: 값이 A → B → A 로 돌아오면 CAS는 "안 바뀌었다"고 판단해 성공한다. 정수
카운터에서는 상관없다. 문제가 되는 건 **포인터**다 — 노드가 free되고 같은 주소에 다른
노드가 malloc되면 주소는 같지만 내용이 다르다. 해법은 남는 비트에 세대 카운터를 넣거나
(tagged pointer), hazard pointer / epoch reclamation이다
([B7 §3.4](B7_lockfree_patterns.md)에서 다시 나온다).

### 4.3 float은 비트로 옮겨 담는다

원본 ALS 문제의 payload는 `float lux` 하나다. `_Atomic float`도 이 기계에서는 lock-free지만
(§3.7 실측), 실전에서는 정수로 바꿔 담는다. 이유가 셋이다.

1. **컴파일 타임에 확인할 방법이 없다.** C11은 `ATOMIC_INT_LOCK_FREE`, `ATOMIC_POINTER_LOCK_FREE` 같은 매크로만 정의하고 `ATOMIC_FLOAT_LOCK_FREE`는 **존재하지 않는다**(직접 확인). `_Static_assert`를 걸 수 없어, 조용히 libatomic 락으로 떨어지는 툴체인에서 알아챌 방법이 없다.
2. 락으로 떨어지면 "wait-free reader"라는 주장이 거짓이 되고 ISR에서 못 쓴다. 게다가 부동소수점 atomic은 툴체인 편차가 가장 큰 영역이다(soft-float ABI, `arm-none-eabi`, 옛 gcc). 정수는 그런 게 없다.

`recent_lux_solution.c` 가 하는 방식이다.

```c
/* 초기값 = quiet NaN 의 비트패턴: "아직 유효한 값 없음" */
static _Atomic uint32_t g_latest_bits = 0x7FC00000u;
_Static_assert(sizeof(float) == sizeof(uint32_t), "float이 32비트여야 한다");

void publish_lux(float lux)
{
    uint32_t bits;
    memcpy(&bits, &lux, sizeof bits);          /* UB 없는 type-pun */
    atomic_store_explicit(&g_latest_bits, bits, memory_order_release);
}

/* reader: load(acquire) → memcpy → isnan(lux) 이면 "아직 값 없음"으로 -1 */
```

**왜 `memcpy`이고 `*(uint32_t *)&lux`가 아닌가**: strict aliasing 규칙(C11 §6.5p7) 때문이다.
객체의 값은 그 객체와 호환되는 타입의 lvalue로만 읽어야 한다. `float` 객체를 `uint32_t *`로
읽는 건 UB이고, `-O2`에서 컴파일러는 "이 둘은 겹치지 않는다"고 가정해 스토어를 지우거나
옛 값을 재사용한다. `memcpy`는 `unsigned char` 단위 복사로 정의되어 예외다. C에서는 union
type-pun도 합법이지만, `memcpy`가 C/C++ 양쪽에서 통하는 습관이다.

공짜냐? 공짜다. `-O2` ARM64에서 `publish_lux` 전체가 이것뿐이다.

```
	fmov	w8, s0        ; memcpy → 레지스터 이동 하나
	stlr	w8, [x9]      ; release store
	ret
```

**초기값이 NaN인 이유**: "아직 값 없음"을 별도 bool 없이 표현한다. `02_modem_rssi_window`는
같은 문제를 다르게 푼다 — 타임스탬프에 `+1`을 해서 워드 전체가 `0`이면 "없음"으로 쓴다.

## 5. 단계별로 만들어 보기 — "데이터 먼저, 깃발 나중"

lock-free 코드의 90%가 이 한 패턴이다. **생산자가 데이터를 다 쓴 뒤 깃발을 세우고, 소비자는
깃발을 본 뒤 데이터를 읽는다.** SPSC 링버퍼도, seqlock도, 포인터 교체도 전부 이것의 변형이다.

### 5.1 v0 — `volatile` (틀렸다)

```c
static int          g_payload;     /* 실제 데이터 */
static volatile int g_ready;       /* "다 됐다" 깃발 */

/* producer */  g_payload = 42;                 /* 1) 데이터 */
                g_ready   = 1;                  /* 2) 깃발  */

/* consumer */  while (!g_ready) { }            /* 깃발을 기다린다 */
                printf("%d\n", g_payload);
```

돌려보면 `payload=42`가 나온다. **그래서 위험하다.** TSan은 §3.4에서 본 대로 race라고 하고,
실제 ARM 하드웨어에서 깨진다.

```svg
<svg viewBox="0 0 700 250" role="img" aria-label="순서가 뒤집힌 타임라인">
  <text x="350" y="18" text-anchor="middle">v0 — 두 스토어가 뒤집혀 보이는 순간</text>
  <text class="lbl" x="20" y="46">생산자 코어</text><line class="muted" x1="20" y1="56" x2="670" y2="56"/>
  <rect class="fill-soft" x="70" y="64" width="120" height="28" rx="6"/><text class="lbl" x="130" y="83" text-anchor="middle">payload = 42</text>
  <rect class="fill-soft" x="240" y="64" width="120" height="28" rx="6"/><text class="lbl" x="300" y="83" text-anchor="middle">ready = 1</text>

  <text class="lbl" x="20" y="136">메모리에 보이는 순서</text><line class="muted" x1="20" y1="146" x2="670" y2="146"/>
  <rect class="box" x="240" y="154" width="120" height="28" rx="6"/><text class="lbl" x="300" y="173" text-anchor="middle">ready = 1</text>
  <rect class="box" x="470" y="154" width="130" height="28" rx="6"/><text class="lbl" x="535" y="173" text-anchor="middle">payload = 42</text>
  <line class="accent dash" x1="130" y1="92" x2="535" y2="152"/>
  <text class="lbl" x="320" y="124">payload 스토어가 늦게 도착한다</text>

  <text class="lbl" x="20" y="216">소비자 코어</text><line class="muted" x1="20" y1="226" x2="670" y2="226"/>
  <rect class="box" x="370" y="194" width="90" height="26" rx="6"/><text class="lbl" x="415" y="212" text-anchor="middle">ready 봄</text>
  <rect class="box" x="370" y="232" width="185" height="26" rx="6"/><text class="lbl" x="462" y="250" text-anchor="middle">payload 읽음 → 쓰레기</text>
  <line class="accent" x1="300" y1="182" x2="408" y2="194"/>
</svg>
```

### 5.2 v1 — `_Atomic` + `relaxed` (아직 틀렸다)

원자성만 얻고 순서는 안 얻은 경우다. 초보자가 두 번째로 하는 실수다.

```c
/* producer */  g_payload = 42;
                atomic_store_explicit(&g_ready, true, memory_order_relaxed);
/* consumer */  while (!atomic_load_explicit(&g_ready, memory_order_relaxed)) { }
                printf("%d\n", g_payload);      /* ← 여기가 race 다 */
```

TSan은 이제 깃발이 아니라 **payload**를 정확히 지목한다. relaxed는 깃발 자체의 race만
없애고 payload를 보호하지 못한다는 뜻이다.

```
WARNING: ThreadSanitizer: data race (pid=8999)
  Read of size 1 at 0x0001044d8004 by thread T1:
    #0 consumer v2_relaxed.c:17
  Previous write of size 1 at 0x0001044d8004 by thread T2:
    #0 producer v2_relaxed.c:11
  Location is global 'g_payload' at 0x0001044d8004
SUMMARY: ThreadSanitizer: data race v2_relaxed.c:17 in consumer
```

### 5.3 v2 — `release` / `acquire` (맞다)

```c
static int           g_payload;    /* 평범한 int로 남는다 */
static _Atomic bool  g_ready;      /* 깃발만 atomic */

/* producer */  g_payload = 42;
                atomic_store_explicit(&g_ready, true, memory_order_release);

/* consumer */  while (!atomic_load_explicit(&g_ready, memory_order_acquire)) { }
                printf("%d\n", g_payload);      /* 42 가 보장된다 */
```

TSan 경고 0. 그리고 `g_payload`는 **여전히 평범한 `int`**다. 이게 핵심이다 — 깃발 하나에
release/acquire를 걸면 그 앞뒤의 평범한 데이터까지 보호된다.

```svg
<svg viewBox="0 0 700 245" role="img" aria-label="release acquire가 금지하는 것">
  <text x="350" y="18" text-anchor="middle">release / acquire — 한쪽으로만 막는 문</text>
  <text class="lbl" x="20" y="46">생산자</text><line class="muted" x1="20" y1="54" x2="330" y2="54"/>
  <rect class="box" x="40" y="62" width="130" height="28" rx="6"/><text class="lbl" x="105" y="81" text-anchor="middle">payload = 42</text>
  <rect class="fill-soft" x="190" y="62" width="130" height="28" rx="6"/><text class="lbl" x="255" y="81" text-anchor="middle">store(ready, rel)</text>
  <line class="accent" x1="255" y1="98" x2="255" y2="126"/><line class="accent" x1="255" y1="126" x2="150" y2="126"/>
  <text class="lbl" x="40" y="118">앞의 쓰기는</text><text class="lbl" x="40" y="140">뒤로 못 넘어온다</text>

  <text class="lbl" x="380" y="46">소비자</text><line class="muted" x1="380" y1="54" x2="680" y2="54"/>
  <rect class="fill-soft" x="390" y="62" width="130" height="28" rx="6"/><text class="lbl" x="455" y="81" text-anchor="middle">load(ready, acq)</text>
  <rect class="box" x="540" y="62" width="130" height="28" rx="6"/><text class="lbl" x="605" y="81" text-anchor="middle">payload 읽기</text>
  <line class="accent" x1="455" y1="98" x2="455" y2="126"/><line class="accent" x1="455" y1="126" x2="580" y2="126"/>
  <text class="lbl" x="490" y="118">뒤의 읽기는</text><text class="lbl" x="490" y="140">앞으로 못 넘어간다</text>

  <line class="accent dash" x1="255" y1="62" x2="455" y2="62"/>
  <text class="lbl" x="355" y="56" text-anchor="middle">synchronizes-with</text>
  <rect class="box" x="40" y="170" width="630" height="62" rx="8"/>
  <text class="lbl" x="55" y="194">acquire 로드가 그 release 스토어가 쓴 값을 실제로 읽었을 때만 짝이 성립한다.</text>
  <text class="lbl" x="55" y="216">그 순간 release 이전의 모든 쓰기가 acquire 이후에 보인다. 한쪽만 쓰면 아무 보장도 없다.</text>
</svg>
```

말로 정확히 적으면 이렇다.

- **release store**: 이 스토어 **앞**에 있는 이 스레드의 읽기/쓰기는 스토어보다 **뒤로 밀리지 않는다**. 뒤의 것이 앞으로 오는 건 막지 않는다 (한쪽 문).
- **acquire load**: 이 로드 **뒤**에 있는 이 스레드의 읽기/쓰기는 로드보다 **앞으로 가지 않는다**.
- **짝이 맞았을 때만**: acquire 로드가 그 release 스토어가 쓴 값을 읽으면 두 스레드가 *synchronizes-with* 관계가 되고, release 이전의 모든 쓰기가 acquire 이후에 보인다. **한쪽만 걸면 아무 보장이 없다.**

### 5.4 `memory_order` — 언제 무엇을 쓰나

| order | 무엇을 보장하나 | 언제 쓰나 | 이 저장소의 어느 코드 |
| --- | --- | --- | --- |
| `relaxed` | 원자성만. 순서 보장 없음 (같은 객체의 수정 순서와 coherence는 지켜진다) | 통계 카운터, seqlock 필드, 이미 락/참조로 보호된 refcount 증가 | `g_errors` (01·02·06), seqlock 필드 전부 (01), `node_ref` (06) |
| `acquire` | 로드. 이 뒤의 접근이 앞으로 못 감. release와 짝이면 그 앞의 쓰기 전부 보임 | 깃발·시퀀스·인덱스를 **읽어서** 데이터를 읽기 시작할 때 | `rssi_get_latest`의 `g_pub` 로드 (02), seqlock 첫 `g_seq` 로드 (01) |
| `release` | 스토어. 이 앞의 접근이 뒤로 못 감 | 데이터를 다 쓰고 깃발·인덱스를 **세울** 때 | `publish()`의 `g_pub` 스토어 (02), `g_running` 스토어 (01·02) |
| `acq_rel` | RMW 전용. 로드 부분은 acquire, 스토어 부분은 release | `fetch_sub`로 refcount를 0까지 내릴 때, lock-free 스택의 CAS | `node_unref`의 `fetch_sub` (06) |
| `seq_cst` | 위의 전부 + 모든 `seq_cst` 연산에 대한 **단일 전역 순서**가 존재 | 기본값. 의심스러우면 이것. 두 깃발을 교차로 보는 패턴(Dekker)에는 이것만 맞다 | `atomic_store(&g_running, false)` 처럼 `_explicit` 없이 쓴 곳 (06) |

`memory_order_consume` 는 **쓰지 말 것** — 어느 컴파일러도 제대로 구현하지 않아 전부 acquire로 승격한다.

### 5.5 컴파일러 재배치와 CPU 재배치는 다르다

두 층이 **각각** 뒤집는다. 하나만 막아도 안 된다.

| 층 | 무엇을 하나 | 무엇으로 막나 |
| --- | --- | --- |
| 컴파일러 | 로드를 루프 밖으로 올린다, 스토어를 합친다, 순서를 바꾼다 | `volatile`도 막는다. atomic도 막는다 |
| CPU | store buffer, 추측 실행, 캐시 라인 도착 순서 | **배리어 명령어만** 막는다. `volatile`은 배리어를 안 내보낸다 |

컴파일러 층이 얼마나 과격한지는 `-O2` 결과에 그대로 나온다. 평범한 `unsigned head, tail`로
SPSC 링을 짜면 소비자의 대기 루프가 문자 그대로 `b LBB2_4`(자기 자신으로 점프) 하나로
컴파일된다 — 큐가 비면 영원히 돈다. 실제 기계어는 [B7 §5](B7_lockfree_patterns.md)에 있다.
값이 이상해지는 게 아니라 **프로그램이 멈춘다**는 것이 "UB는 이상한 값보다 나쁘다"의 실물이다.

CPU 층은 ARMv8에서 이렇게 번역된다 (Apple M2, Apple clang 21, `-O2` 실측).

| C | ARM64 명령어 |
| --- | --- |
| `store(relaxed)` / `load(relaxed)` | `str` / `ldr` |
| `store(release)` | `stlr` |
| `store(seq_cst)` | `stlr` (release와 같다) |
| `load(acquire)` | `ldar`, 또는 FEAT_LRCPC 있는 코어에서 `ldapr` |
| `load(seq_cst)` | `ldar` (`ldapr`은 너무 약해서 안 쓴다) |
| `fetch_add(relaxed / release / acq_rel)` | `ldadd` / `ldaddl` / `ldaddal` |
| `thread_fence(release)` / `thread_fence(acquire)` | `dmb ish` / `dmb ishld` |

- **release/acquire는 보통 배리어 명령이 아니라 로드/스토어 자체에 붙는다** (`stlr`, `ldar`). 그래서 `dmb`를 따로 넣는 것보다 싸다. ARMv8에서 `seq_cst`가 같은 명령어인 것은 `stlr`/`ldar`가 원래 sequentially-consistent(RCsc) 의미이기 때문이다. x86에서도 load/store는 거의 공짜이지만 seq_cst store만 `xchg`로 무거워진다 — **"seq_cst는 항상 느리다"도 "항상 공짜다"도 틀렸다.**
- `atomic_thread_fence`는 **독립 배리어**다(`dmb`). 특정 객체에 붙지 않는다. 01의 seqlock이 이걸 쓰는 이유는 [B7 §4.2](B7_lockfree_patterns.md)에 있다.

### 5.6 판단표 — 언제 atomic, 언제 mutex

| 상황 | 고르는 것 | 이유 |
| --- | --- | --- |
| payload가 64비트 안에 들어간다 (값+시각, bool, 카운터, 포인터) | **atomic 하나** | reader가 로드 하나. 찢어질 수 없고 블로킹이 없다 |
| payload가 작은 구조체 (40 B 정도), writer 1명, reader 많음, writer가 기다려선 안 됨 | **seqlock** | writer wait-free. 대가는 reader 재시도 |
| payload가 크거나(프레임), 불변 객체 전체를 바꿈 | **포인터 교체 + refcount** | 복사 대신 포인터 하나. 대신 수명 관리가 생긴다 |
| 그 외 전부 — 여러 필드를 함께 갱신, 자료구조 조작, 조건 대기 | **mutex** (필요하면 condvar) | 10줄이고 명백히 맞다. 경합 없는 pthread mutex는 syscall 없이 수십 ns |
| 락을 잡은 채 syscall / malloc / 벤더 블로킹 호출 | **하지 말 것** | 200 ms 동안 모든 reader가 멈춘다. 락은 포인터 한 번 바꿀 동안만 |

> **측정 없는 lock-free 금지.** lock-free 코드는 리뷰어가 검증할 수 없고, 버그가 며칠에 한 번
> 프로덕션에서만 나온다. `06_config_publish_rollback` 모범답안이 아주 짧은 mutex를 쓰는 이유가
> 파일 머리에 그대로 적혀 있다 — *"the lock wins: ... it is ~10 lines instead of ~200 that a
> reviewer has to trust."* 면접에서도 이렇게 말하는 게 맞다: **"기본은 mutex다. atomic으로
> 가는 근거는 이 셋뿐이다 — 64비트에 다 들어간다 / writer가 절대 기다릴 수 없다 /
> 프로파일이 이 락을 지목했다."**

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
| --- | --- | --- | --- |
| 깃발을 `volatile`로 선언하고 끝 | 대부분 잘 돌다가 릴리즈 빌드/다른 보드에서 드물게 깨진 데이터 | `volatile`은 CPU 배리어를 내보내지 않는다. 원자성도 없다 | 깃발을 `_Atomic`으로, store는 `release`, load는 `acquire` |
| 전부 `relaxed`로 통일 | TSan이 payload를 race로 지목. 값이 간헐적으로 쓰레기 | relaxed는 원자성만 준다. 순서는 하나도 안 준다 | 데이터를 발행하는 지점만 `release`/`acquire`로 올린다 |
| release만 걸고 reader는 relaxed (또는 반대) | 재현이 거의 안 되는 간헐 버그 | synchronizes-with는 **짝**으로만 성립한다 | 항상 쌍으로 본다. 리뷰 때 짝을 손으로 짚는다 |
| 구조체에 `_Atomic` | 벤치마크가 mutex보다 느림. ISR에서 데드락 | 하드웨어 명령이 없으면 컴파일러가 락을 숨긴다 | `atomic_is_lock_free()`로 확인. 안 되면 packing이나 seqlock |
| `float`/`double`을 그대로 `_Atomic` | 어떤 툴체인에서 조용히 libatomic 락 | 부동소수점 atomic은 컴파일 타임 확인 매크로가 없다 | 비트를 `_Atomic uint32_t`/`uint64_t`에 `memcpy`로 담는다 |
| `*(uint32_t *)&f`로 비트 꺼내기 | `-O2`에서만 값이 틀림 | strict aliasing 위반 = UB | `memcpy`. 비용은 0 (`fmov` 하나) |
| `fetch_add` 반환값을 "바뀐 뒤 값"으로 씀 | off-by-one. refcount에서 free 시점이 하나 밀림 | `fetch_*`는 **바뀌기 전** 값을 준다 | `fetch_sub(...) == 1` 이 "내가 마지막"이다 |

## 7. 손으로 확인하기

```sh
cd 02_modem_rssi_window && ./main.sh sol     # packed atomic word — 값+시각이 어긋나는지
```

하네스가 "존재한 적 없는 (값, 나이) 조합"을 찾는다. 그 다음 직접 깨뜨려 본다 —
`rssi_window_solution.c` 를 복사해 `g_pub` 하나를 `_Atomic int g_rssi` + `_Atomic uint64_t g_ts`
두 개로 쪼갠다. 둘 다 `seq_cst`로 해도 하네스가 깨진다. **여기서 배운다.**

TSan은 이 노트의 모든 주장을 검증하는 도구다.

```sh
cd 01_gps_fix_cache
cc -std=c11 -O2 -Wall -Wextra -pthread -fsanitize=thread -g \
   -o /tmp/t main.c gps_cache_solution.c -lm && /tmp/t
# → ALL CHECKS PASSED, TSan 경고 0. 필드가 전부 _Atomic 이기 때문이다.
#   하나라도 평범한 double 로 바꾸면 경고가 뜬다 (B7 §7 에 실습 절차가 있다).
```

기계어를 직접 보는 것도 5분이면 된다 — 작은 `.c` 에 relaxed/release 스토어를 하나씩 쓰고
`cc -std=c11 -O2 -S -o - x.c` 로 어셈블리를 보면 `str` 와 `stlr` 가 그대로 보인다.

## 8. 자가 점검

```check
Q: `volatile int flag;` 로 두고 `while (!flag) {}` 로 기다리면 무엇이 보장되고 무엇이 안 되나?
A: 보장되는 것은 컴파일러가 그 읽기를 지우거나 루프 밖으로 올리지 않는다는 것뿐이다. 즉 무한 루프로 컴파일되지는 않는다.
A: 보장되지 않는 것: 원자성(읽기-수정-쓰기는 여전히 쪼개진다), CPU 재배치 차단(배리어 명령이 안 나간다), 그리고 flag 앞에 쓴 다른 데이터가 보인다는 보장. 표준상 여전히 data race = UB이고 TSan도 race로 잡는다.

Q: `memory_order_relaxed` atomic으로 깃발을 세우면 왜 부족한가? 무엇이 남는가?
A: relaxed는 원자성만 준다. 깃발 자체의 data race는 사라지지만 깃발 앞에 쓴 payload와의 순서는 전혀 보장하지 않는다.
A: 실제로 TSan이 깃발이 아니라 payload를 race로 지목한다. 소비자가 깃발을 먼저 보고 payload를 아직 안 쓰인 상태로 읽을 수 있다.

Q: release와 acquire를 각각 한 문장으로 정확히 말해 보라. 왜 "짝"이어야 하나?
A: release store는 이 스토어 앞의 이 스레드 접근이 스토어보다 뒤로 밀리지 않게 한다. acquire load는 이 로드 뒤의 접근이 로드보다 앞으로 가지 않게 한다. 둘 다 한쪽으로만 막는 문이다.
A: 짝이어야 하는 이유는 synchronizes-with 관계가 "acquire 로드가 그 release 스토어가 쓴 값을 실제로 읽었을 때" 성립하기 때문이다. 한쪽만 걸면 상대 스레드에 대한 보장이 하나도 생기지 않는다.

Q: `struct GpsFix` (40바이트)에 `_Atomic`을 붙이면 무슨 일이 일어나나?
A: 컴파일이 되고 동작도 맞지만, 40바이트 원자 스토어 명령이 없으므로 컴파일러가 보이지 않는 락을 붙인다. 실측으로 `atomic_is_lock_free()`가 0을 돌려준다.
A: 문제는 두 가지다. "wait-free reader"라는 주장이 거짓이 되고, ISR이나 시그널 핸들러에서 쓰면 데드락이 가능해진다. 40바이트면 seqlock이나 mutex 스냅샷으로 가야 한다.

Q: `compare_exchange_weak`는 왜 값이 같아도 실패할 수 있나? 그럼 언제 `strong`을 쓰나?
A: ARM·RISC-V 같은 LL/SC 기계에서 `ldxr`/`stxr` 짝으로 구현되고, 그 사이에 인터럽트가 나거나 다른 코어가 같은 캐시 라인을 건드리면 `stxr`가 값과 무관하게 실패한다. 이것이 spurious failure다.
A: 이미 루프를 돌 코드라면 weak이 맞다 — 가짜 실패든 진짜 실패든 다시 돌면 된다. strong은 내부적으로 재시도 루프를 갖고 있으므로, 한 번만 시도하고 실패하면 다른 일을 할 때만 쓴다.

Q: float 값을 발행할 때 `_Atomic float` 대신 `_Atomic uint32_t` + `memcpy`를 쓰는 이유 두 가지는?
A: 첫째, 부동소수점 atomic이 lock-free인지 컴파일 타임에 확인할 방법이 없다 — C11에는 `ATOMIC_FLOAT_LOCK_FREE` 매크로가 아예 없다. 조용히 libatomic 락으로 떨어져도 알아챌 수 없고 `_Static_assert`도 못 건다.
A: 둘째, 정수 atomic은 툴체인 편차가 없다. 그리고 `memcpy`는 비용이 0이다 — `-O2`에서 `fmov` 레지스터 이동 하나로 컴파일된다.

Q: `*(uint32_t *)&my_float` 로 비트를 꺼내면 안 되는 이유는?
A: strict aliasing 위반이다(C11 §6.5p7). 객체는 호환되는 타입의 lvalue로만 접근해야 하고, 위반은 UB다.
A: 증상이 나쁘다 — `-O0`에서는 잘 돌고 `-O2`에서만 틀린다. 컴파일러가 "float과 uint32_t는 겹치지 않는다"고 가정해 스토어를 지우거나 옛 값을 재사용한다. `memcpy`는 unsigned char 단위로 정의되어 있어 예외다.

Q: 면접에서 "왜 mutex 안 쓰고 atomic 썼냐"고 물으면?
A: 기본은 mutex라고 먼저 말한다. 그 다음 atomic을 고른 근거를 셋 중 하나로 댄다 — payload가 64비트 안에 다 들어간다 / writer(또는 reader)가 절대 기다려서는 안 되는 경로다 / 프로파일이 이 락을 지목했다.
A: 그리고 대가를 같이 말한다. lock-free는 리뷰어가 검증하기 어렵고 버그가 간헐적이다. `06`의 모범답안이 아주 짧은 mutex를 고르고 "리뷰어가 믿어야 할 줄이 200줄에서 10줄로 줄어든다"고 적어 둔 것이 그 판단의 예다.
```

## 9. 요약 카드

- 문제는 셋이다 — **원자성 / 가시성 / 순서**. `_Atomic`은 ①을 주고, `memory_order`가 ③을 준다.
- `volatile`은 **접근을 지우지 말라**는 뜻일 뿐. 원자성 없음, CPU 배리어 없음, data race는 그대로 UB.
- `volatile`의 진짜 자리 셋: **MMIO 레지스터 / `volatile sig_atomic_t` / `setjmp` 지역변수**.
- `_explicit` 없이 쓰면 `seq_cst`. **의심스러우면 이게 정답이다.**
- 패턴 하나만 외운다: **데이터 먼저 → `store(release)`** / **`load(acquire)` → 데이터 읽기.** 짝으로만.
- `relaxed`는 통계 카운터 · seqlock 필드 · 이미 보호된 refcount 증가. 그 밖에는 쓰지 말 것.
- 구조체에 `_Atomic` 금지. 64비트 packing → seqlock → mutex 순. float은 비트를 `_Atomic uint32_t`에 `memcpy`로 (포인터 캐스트는 strict aliasing 위반).
- `fetch_*`는 **바뀌기 전** 값을 준다. `fetch_sub(...) == 1` 이 "내가 마지막 참조".
- **측정 없는 lock-free 금지.** 기본은 mutex, 락은 포인터 한 번 바꿀 동안만.

---

다음: [B7. 락 없이 공유하는 세 가지 패턴](B7_lockfree_patterns.md) — SPSC 링버퍼 · seqlock · 포인터 교체 + refcount.
