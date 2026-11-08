---
id: DS-M03
time: 90분
updated: 2026-11-07
---
# DS-M03 간소화 슬랩 할당기

## 학습 목표

- 요청 크기를 **크기 클래스(size class)** 16/32/64/128B 로 올림해 같은 크기끼리 한 **슬랩(slab)** 에 모으는 이유를 설명하고, 클래스 계산을 루프·CLZ·표 세 가지로 구현한다.
- 정적 4KB 정렬 아레나에서 `p & ~0xFFF` **주소 마스킹**으로 포인터 → 슬랩 헤더를 O(1)에 찾고, 그 덕에 `free(p)` 에 크기 인자가 필요 없음을 보인다.
- 캐시(cache)마다 **partial / full / empty** 슬랩 리스트를 두고, 할당·해제 때 슬랩이 리스트 사이를 옮겨 다니는 상태 기계를 불변식 검사기로 검증한다.
- 잘못된 포인터(아레나 밖, 객체 중간, 헤더, 꼬리, 미사용 슬랩)와 **이중 해제(double free)** 를 쿠키 + 느린 확인으로 잡는다.
- 메시지 파이프라인 트레이스로 **내부 단편화(internal fragmentation)** 를 재고, 시스템 `malloc` 과 속도·단편화를 실측 비교한다.

## 1. 문제

펌웨어에서 "작은 객체를 자주 만들고 버리는" 곳 — 네트워크 버퍼 디스크립터, RTOS 메시지, 센서 샘플, 로그 레코드 — 은 범용 `malloc` 대신 **크기별 전용 캐시**를 쓴다. Linux 커널의 `kmalloc()` / `kmem_cache_alloc()` 이 바로 슬랩 할당기이고, 면접에서는 "고정 풀(DS-M02)을 여러 크기로 일반화하라", "`free(p)` 는 크기를 어떻게 아나?", "빈 슬랩은 언제 돌려주나?" 가 꼬리질문으로 이어진다.

구조는 세 층이다.

1. **아레나(arena)**: `static _Alignas(4096) slab_page_t g_arena[64]` — 256KB 정적 배열. 시작 주소가 4096의 배수라서 모든 슬랩도 4KB 경계에서 시작한다. 할당기 안에 `malloc` 은 없다.
2. **슬랩**: 4KB 한 장 = `[64B 헤더 | 같은 크기 객체 × nobj | 남는 꼬리]`. free 객체들은 객체 안에 내장된 단일 연결 리스트로 이어진다(첫 8바이트 = next, DS-M02의 내장 free list와 같은 아이디어).
3. **캐시**: 크기 클래스마다 하나. 슬랩들을 **partial**(일부 사용), **full**(꽉 참), **empty**(모두 비어 있음) 세 리스트로 나눠 들고 있다. 아무 캐시에도 속하지 않은 슬랩은 **전역 풀**에 있다.

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">아레나 = 4KB 정렬 슬랩 64장, 슬랩 하나(64B 클래스)의 내부, 그리고 주소 마스킹</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="80" height="34" class="f-mut"/><rect x="100" y="40" width="80" height="34" class="f-mut"/>
    <rect x="180" y="40" width="80" height="34" class="f-mut"/><rect x="260" y="40" width="80" height="34" class="f-mut"/>
    <rect x="340" y="40" width="80" height="34" class="f-mut"/><rect x="420" y="40" width="80" height="34" class="f-acc"/>
    <rect x="500" y="40" width="80" height="34" class="f-mut"/><rect x="580" y="40" width="140" height="34" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="60" y="62">slab 0</text><text x="140" y="62">slab 1</text><text x="220" y="62">slab 2</text><text x="300" y="62">slab 3</text>
    <text x="380" y="62">slab 4</text><text x="460" y="62">slab 5</text><text x="540" y="62">slab 6</text><text x="650" y="62">… slab 63</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="90">base</text><text x="100" y="90">+0x1000</text><text x="420" y="90">+0x5000</text><text x="500" y="90">+0x6000</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-dasharray="4 3"><path d="M420 74 L20 120"/><path d="M500 74 L720 120"/></g>
  <rect x="20" y="120" width="90" height="46" class="f-warn" stroke="currentColor"/>
  <g stroke="currentColor" fill="none">
    <rect x="110" y="120" width="60" height="46"/><rect x="170" y="120" width="60" height="46"/><rect x="230" y="120" width="60" height="46"/>
    <rect x="290" y="120" width="60" height="46"/><rect x="350" y="120" width="140" height="46"/>
    <rect x="490" y="120" width="60" height="46"/><rect x="550" y="120" width="60" height="46"/><rect x="610" y="120" width="110" height="46"/>
  </g>
  <rect x="170" y="120" width="60" height="46" class="f-ok" stroke="currentColor"/>
  <rect x="290" y="120" width="60" height="46" class="f-ok" stroke="currentColor"/>
  <rect x="550" y="120" width="60" height="46" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="65" y="139">헤더 64B</text><text x="65" y="157" font-family="monospace">free·inuse·cls</text>
    <text x="140" y="148">obj 0</text><text x="200" y="148">obj 1</text><text x="260" y="148">obj 2</text><text x="320" y="148">obj 3</text>
    <text x="420" y="148">…</text><text x="520" y="148">obj 40</text><text x="580" y="148">obj 41</text><text x="665" y="148">… obj 62</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M45 166 C 60 205, 180 205, 195 168"/><path d="M195 168 l-2 9 M195 168 l7 6"/>
    <path d="M205 166 C 230 200, 300 200, 318 168"/><path d="M318 168 l-2 9 M318 168 l7 6"/>
    <path d="M325 166 C 380 215, 540 215, 578 168"/><path d="M578 168 l-3 9 M578 168 l7 5"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="232">free 객체(초록)는 첫 8바이트에 다음 free 객체 주소를 담는다 → 추가 메모리 0</text>
  </g>
  <rect x="20" y="248" width="700" height="98" fill="none" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="13">
    <text x="34" y="272">p              = base + 0x5A40      (사용자가 free 로 넘긴 포인터)</text>
    <text x="34" y="294">p &amp; ~0xFFF     = base + 0x5000      → slab 5 의 헤더 (슬랩이 4KB 정렬이라 가능)</text>
    <text x="34" y="316">off = 0xA40 − 64 = 2560,  2560 % 64 = 0 → 객체 경계 OK,  2560 / 64 = 40 → obj 40</text>
    <text x="34" y="338">크기는 헤더의 cls 에서 → free(p) 에 크기 인자가 필요 없다</text>
  </g>
</svg>
```

크기 클래스와 슬랩 하나에 들어가는 객체 수 (`(4096 − 64) / size`):

| 클래스 | 요청 크기 | nobj | 꼬리 낭비 | 헤더+꼬리 / 4096 |
|---|---|---|---|---|
| 0 | 1–16 | 252 | 0 | 1.6% |
| 1 | 17–32 | 126 | 0 | 1.6% |
| 2 | 33–64 | 63 | 0 | 1.6% |
| 3 | 65–128 | 31 | 64 | 3.1% |

### Part A — 크기 클래스, 주소 마스킹, 슬랩 하나

- `size_to_class(n)` — 1..16→0, 17..32→1, 33..64→2, 65..128→3. `n == 0` 이나 `n > 128` 은 −1.
- `slab_of(p)` — `(uintptr_t)p & ~SLAB_MASK` 로 슬랩 헤더 주소.
- `slab_format(s, cls)` — 헤더(magic, cls, inuse=0, nobj)를 채우고, 모든 객체를 **주소 순서로** free 리스트에 이어 놓고, 각 free 객체의 두 번째 워드에 `FREE_COOKIE` 를 쓴다.
- `slab_obj_alloc(s)` / `slab_obj_free(s, p)` — 내장 free 리스트 pop / push. 할당할 때 쿠키를 지운다.
- 테스트: 64B 클래스 슬랩 하나에서 63개를 모두 꺼내 주소 순서·정렬·`slab_of` 를 확인, 64번째는 NULL, 세 개를 해제하면 **LIFO** 로 다시 나오는지.

### Part B — 캐시: partial / full / empty 와 해제 검증

- `slab_lookup(p)` — p 가 "사용 중인 슬랩의 객체 시작 주소"일 때만 그 슬랩, 아니면 NULL.
- `kc_alloc(c)` — partial 머리 → 없으면 empty 슬랩을 partial 로 → 없으면 전역 풀에서 새 슬랩을 꺼내 포맷 → 그것도 없으면 NULL. 슬랩이 꽉 차면 full 로.
- `slab_free(p)` — `SF_OK / SF_BAD_PTR / SF_DOUBLE`. full 이던 슬랩은 partial 로, 완전히 빈 슬랩은 **캐시당 1개(EMPTY_KEEP)까지** empty 리스트에 남기고 나머지는 전역 풀로.
- `slab_reap()` — 모든 캐시의 empty 슬랩을 전역 풀로 반납(Linux `kmem_cache_shrink()` 에 해당).
- 테스트: 상태 전이 시나리오, 잘못된 해제 7종, 아레나 소진 후 다른 클래스로 회수, 랜덤 60000회 참조 모델 대조(매 연산 `slab_check()`).

### Part C — kmalloc 스타일 앞단과 측정

- `slab_malloc(n)` — 클래스를 골라 `kc_alloc`. `slab_usable_size(p)` — 그 객체의 실제 크기(우리 객체가 아니면 0).
- 센서 허브 메시지 파이프라인: BLE 알림 20B, IMU 샘플 24B, 로그 48B, CAN-FD 프레임 72B, NMEA 문장 82B 를 FIFO 큐로 생산·소비 100000회. 내용이 깨지지 않는지 확인하고 **내부 단편화**와 피크 슬랩 사용률을 출력한다.
- `demo_bench.c` 에서 같은 할당기를 시스템 `malloc` 과 속도·`malloc_size()` 로 비교한다.

> Linux 커널의 SLAB → SLUB 역사, `kmem_cache`, per-CPU 슬랩 같은 운영체제 쪽 이야기는 OS 트랙(OS-M05 segregated list, OS-M06 tcache/arena)에서 다룬다 (see:OS).

## 2. Problem (English)

> **Simplified slab allocator over a static arena.**
>
> You are given a 256 KB static arena aligned to 4 KB, split into 64 slabs of 4096 bytes. Each slab begins with a 64-byte header; objects of one size class follow. Size classes are 16, 32, 64 and 128 bytes. Do not call `malloc` inside the allocator.
>
> 1. `int size_to_class(size_t n)` → class index, or −1 for `n == 0` or `n > 128`.
> 2. `slab_t *slab_of(const void *p)` → header of the slab containing `p`, in O(1) with no search.
> 3. `void *kc_alloc(kcache_t *c)`: allocate from a partially used slab first, then from a cached empty slab, then from a fresh slab taken from the global pool; return `NULL` only when the cache has no free object and the pool is empty.
> 4. `int slab_free(void *p)`: free any object without being told its size. Return `SF_BAD_PTR` for pointers that are not the start of a live slab's object, and `SF_DOUBLE` for a double free. Keep at most one empty slab per cache; return the rest to the global pool.
> 5. `void *slab_malloc(size_t n)`, `size_t slab_usable_size(const void *p)`.
> 6. Check invariants after every operation (each slab in exactly one list, list matches `inuse`, free-list length = `nobj − inuse`) and compare against a reference model under random operations. Measure internal fragmentation and compare speed with the system `malloc`.
>
> Example: `p = slab_malloc(17)` → `slab_usable_size(p) == 32`; `slab_free(p) == SF_OK`; `slab_free(p) == SF_DOUBLE`; `slab_free(p + 1) == SF_BAD_PTR`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 이 Mac 에서만 필요 (27.2 SDK 링크 깨짐)
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "5/40 passed", exit 1 — TODO 11곳을 채워 40/40 을 만든다
```

TODO: `size_to_class`, `slab_of`, `slab_format`, `slab_obj_alloc`, `slab_obj_free` (Part A) / `slab_lookup`, `kc_alloc`, `slab_free`, `slab_reap` (Part B) / `slab_malloc`, `slab_usable_size` (Part C).
주어지는 것: 원형 이중 리스트(DS-B06), 객체 워드 읽기/쓰기(`obj_next`, `obj_set_next`, `obj_cookie`, `obj_set_cookie` — `memcpy` 로 구현), 전역 풀 `pool_take`/`pool_put`, 리스트 이동 `slab_move`/`slab_to_pool`(카운터 포함), `freelist_contains`, 불변식 검사기 `slab_check()`, 하네스. 리스트 배관은 주어지므로 TODO는 **어떤 슬랩을 언제 어디로 옮기나** 라는 정책에 집중한다.

## 4. 힌트

<details>
<summary>힌트 1 — 크기 클래스는 "올림한 2의 거듭제곱"</summary>

클래스 c 의 크기는 `1 << (4 + c)`. "그 크기가 n 이상이 되는 가장 작은 c" 를 루프로 찾으면 된다. 수식으로는 `c = ceil(log2 n) − 4` 이고 `ceil(log2 n) = 32 − clz(n − 1)` (n ≥ 2). `n − 1 == 0` 일 때 `__builtin_clz(0)` 은 UB라서 n ≤ 16 을 먼저 거른다 → 연습 1.

</details>

<details>
<summary>힌트 2 — 마스킹이 성립하는 세 조건</summary>

① 슬랩 크기가 2^k, ② 모든 슬랩 시작 주소가 2^k 의 배수(아레나를 `_Alignas(4096)` 로), ③ 어떤 객체도 슬랩 경계를 넘지 않는다. 그러면 객체 안의 어떤 바이트 주소든 하위 k 비트만 지우면 그 슬랩의 시작 = 헤더다. 포인터를 정수로 바꿔 마스크하고(`uintptr_t`), 다시 포인터로 돌린다.

</details>

<details>
<summary>힌트 3 — kc_alloc 의 우선순위</summary>

partial → empty → 풀. partial 을 먼저 쓰는 이유는 객체를 **적은 수의 슬랩에 모아야** 다른 슬랩이 완전히 비어 반납될 수 있기 때문이다. 새 슬랩은 `pool_take()` → `slab_format(s, c->cls)` → `slab_move(c, s, ST_PARTIAL)`. 객체를 꺼낸 뒤 `inuse == nobj` 면 full 로.

</details>

<details>
<summary>힌트 4 — slab_lookup 은 다섯 가지를 거른다</summary>

아레나 밖(정수로 비교 — 서로 다른 객체의 포인터를 `<` 로 비교하면 UB), 풀에 있는 슬랩(state, magic), 헤더 영역(`a < obj0`), 객체 중간(`off % size != 0`), 꼬리(`off / size >= nobj`). 128B 클래스는 31개 × 128 = 3968 이라 오프셋 64 + 3968 = 4032 부터 64바이트가 꼬리다.

</details>

<details>
<summary>힌트 5 — 이중 해제: 빠른 의심 + 느린 확인</summary>

free 된 객체에는 항상 쿠키가 있고 살아 있는 객체는 할당 때 쿠키를 지웠다. 그래서 "쿠키가 없다" → 확실히 살아 있음(빠른 경로). "쿠키가 있다" → 이중 해제이거나 사용자 데이터가 우연히 같은 값 → 슬랩 free 리스트를 훑어 확인(최대 nobj). glibc tcache 의 `key` 필드와 같은 방식이다.

</details>

<details>
<summary>힌트 6 — 해제 후 리스트 이동은 두 경우뿐</summary>

`inuse == 0` 이 되면 empty(캐시에 빈 슬랩이 EMPTY_KEEP 개 미만일 때) 또는 풀로. 아니고 원래 full 이었으면 partial 로. partial 에서 partial 로 남는 경우는 아무 것도 안 한다. 순서 주의: `inuse == 0` 검사를 먼저 해야 nobj == 1 같은 경우에도 맞다.

</details>

## 5. 풀이

### Part A — 슬랩 하나: 내장 free 리스트와 마스킹

**불변식(슬랩 하나)**

- 헤더의 `free` 에서 시작하는 리스트 길이 = `nobj − inuse`. 리스트의 모든 원소는 이 슬랩의 객체 경계(`obj0 + i·size`, `i < nobj`)에 있고 쿠키를 갖는다.
- 살아 있는 객체는 리스트에 없다. 리스트에 같은 원소가 두 번 나오면 고리가 생기므로 "길이 ≤ nobj" 검사가 중복도 잡는다.

`slab_format` 은 객체 i 를 i+1 에 잇는다. 그래서 새 슬랩은 낮은 주소부터 나간다(테스트 `p[i] == obj0 + 64·i`). 해제는 머리에 push 하므로 **LIFO** — 방금 해제된 객체가 다음 할당에 다시 나간다. 그 객체는 아직 L1 캐시에 있을 가능성이 높다.

| 연산 | free 리스트 (머리 → ) | inuse |
|---|---|---|
| format (64B) | obj0 → obj1 → … → obj62 | 0 |
| alloc × 63 | (비어 있음) | 63 |
| free obj10, obj40, obj0 | obj0 → obj40 → obj10 | 60 |
| alloc × 3 | obj0, obj40, obj10 순서로 나감 | 63 |

**왜 next 를 `memcpy` 로 읽나**: 아레나의 선언 타입은 `slab_page_t`(union 안의 `uint8_t` 배열)인데 `*(void **)obj` 로 읽고 쓰면 C11 6.5p7 effective type 규칙(strict aliasing) 위반이 된다. `memcpy(&n, o, sizeof n)` 은 어떤 타입이든 바이트 복사라 규칙 밖이고, clang -O2 는 이걸 load 한 번으로 바꾼다. Linux 커널은 `-fno-strict-aliasing` 으로 빌드해서 이 문제를 아예 끈다.

**마스킹의 비용**: AND 한 번. 비교 대상인 "슬랩 밖 메타데이터"(연습 2)는 `meta[(p − base) >> 12]` — 뺄셈·시프트·인덱싱이고 아레나 정렬이 필요 없다. 대신 헤더를 슬랩 밖에 두므로 슬랩마다 객체가 조금 더 들어간다(16B 클래스 252 → 256).

### Part B — 캐시의 상태 기계

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m03a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">슬랩 하나의 일생: 전역 풀 ↔ 캐시의 partial / full / empty 리스트</text>
  <rect x="20" y="120" width="130" height="56" rx="8" class="f-mut" stroke="currentColor"/>
  <rect x="300" y="120" width="140" height="56" rx="8" class="f-acc" stroke="currentColor"/>
  <rect x="580" y="120" width="130" height="56" rx="8" class="f-warn" stroke="currentColor"/>
  <rect x="300" y="230" width="140" height="50" rx="8" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="85" y="144">전역 풀</text><text x="85" y="164" font-size="12">어느 클래스도 아님</text>
    <text x="370" y="144">partial</text><text x="370" y="164" font-size="12">0 &lt; inuse &lt; nobj</text>
    <text x="645" y="144">full</text><text x="645" y="164" font-size="12">inuse == nobj</text>
    <text x="370" y="252">empty</text><text x="370" y="270" font-size="12">inuse == 0, ≤ 1개</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#m03a)">
    <path d="M150 130 C 210 90, 250 90, 298 128"/>
    <path d="M440 132 L578 132"/>
    <path d="M578 166 L442 166"/>
    <path d="M330 178 L330 228"/>
    <path d="M410 228 L410 178"/>
    <path d="M300 262 C 200 262, 110 240, 85 178"/>
    <path d="M300 168 C 230 200, 170 200, 140 178"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="150" y="84">① alloc: 새 슬랩 format</text>
    <text x="450" y="124">② 마지막 객체 alloc</text>
    <text x="450" y="186">③ free 1개</text>
    <text x="240" y="226">④ 마지막 free</text>
    <text x="418" y="210">⑤ alloc</text>
    <text x="20" y="275">⑥ reap: empty → 풀</text>
    <text x="20" y="291">④' empty 가 이미 EMPTY_KEEP 개면 ④ 대신 바로 풀로</text>
    <text x="150" y="215">④'</text>
    <text x="440" y="62">alloc 우선순위: partial → empty → 풀</text>
    <text x="440" y="82">free 는 크기 없이 p &amp; ~0xFFF 로 슬랩 찾기</text>
  </g>
</svg>
```

**불변식(캐시 전체)** — `slab_check()` 가 매 연산 후 확인하는 것:

1. 슬랩 64장은 전역 풀 또는 정확히 한 캐시의 한 리스트에 있다 (리스트를 다 세면 64).
2. 리스트와 상태가 일치: partial 이면 `0 < inuse < nobj`, full 이면 `inuse == nobj`, empty 면 `inuse == 0`. `s->state` 도 그 리스트 이름.
3. 각 슬랩의 free 리스트 길이 = `nobj − inuse`, 원소는 모두 객체 경계 + 쿠키.
4. 카운터 `n_partial/n_full/n_empty` = 실제 리스트 길이, `Σ inuse = live`, `n_empty ≤ EMPTY_KEEP`.

**상태 전이 추적** — Part B 첫 테스트(16B 클래스, 252개/슬랩)의 실제 흐름:

| 단계 | 연산 | 슬랩 A | 슬랩 B | partial | full | empty | 풀 |
|---|---|---|---|---|---|---|---|
| 1 | alloc 1개 | 1 (partial) | — | 1 | 0 | 0 | 63 |
| 2 | alloc 251개 | 252 (full) | — | 0 | 1 | 0 | 63 |
| 3 | alloc 1개 | 252 | 1 (partial) | 1 | 1 | 0 | 62 |
| 4 | free p[5] (A) | 251 (partial) | 1 | 2 | 0 | 0 | 62 |
| 5 | free p[252] (B) | 251 | 0 (empty) | 1 | 0 | 1 | 62 |
| 6 | alloc 1개 | 252 (full) — partial 먼저 | 0 | 0 | 1 | 1 | 62 |
| 7 | A 의 252개 모두 free | 0 → 풀 (empty 자리 이미 1개) | 0 | 0 | 0 | 1 | 63 |
| 8 | `slab_reap()` | — | → 풀 | 0 | 0 | 0 | 64 |

단계 6이 핵심이다. 빈 슬랩 B 가 있어도 반쯤 찬 A 를 먼저 채운다. B 를 썼다면 A 와 B 모두 partial 이 되어 둘 다 반납할 수 없는 상태로 간다.

**EMPTY_KEEP = 1 인 이유 (히스테리시스)**: 빈 슬랩을 즉시 풀로 돌리면, 슬랩 경계에서 alloc/free 가 번갈아 일어날 때마다 "풀에서 꺼내 252개 포맷 → 풀로 반납"이 반복된다(진동, thrashing). 하나를 남겨 두면 그 진동이 사라진다. 대신 그 슬랩은 다른 클래스가 못 쓴다 — 소진 테스트에서 128B 를 전부 해제한 뒤 16B 가 받은 건 `(64 − 1) × 252 = 15876` 개였고, `slab_reap()` 후에야 마지막 슬랩까지 쓸 수 있었다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m03b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">slab_free(p): full 슬랩에서 객체 하나 해제 — 전(왼쪽) / 후(오른쪽)</text>
  <g fill="currentColor" font-size="12"><text x="20" y="50">full: [A]   partial: [B]</text><text x="390" y="50">full: (비어 있음)   partial: [A, B]</text></g>
  <rect x="20" y="64" width="60" height="40" class="f-warn" stroke="currentColor"/>
  <g stroke="currentColor" fill="none">
    <rect x="80" y="64" width="50" height="40"/><rect x="130" y="64" width="50" height="40"/><rect x="180" y="64" width="50" height="40"/><rect x="230" y="64" width="50" height="40"/><rect x="280" y="64" width="60" height="40"/>
  </g>
  <rect x="130" y="64" width="50" height="40" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-size="12"><text x="50" y="82">A 헤더</text><text x="50" y="98" font-family="monospace">free=NULL</text>
    <text x="105" y="88">o0</text><text x="155" y="88">o1 = p</text><text x="205" y="88">o2</text><text x="255" y="88">o3</text><text x="310" y="88">…</text></g>
  <text x="20" y="128" fill="currentColor" font-size="12">inuse = nobj, free 리스트 없음</text>
  <rect x="390" y="64" width="60" height="40" class="f-acc" stroke="currentColor"/>
  <g stroke="currentColor" fill="none">
    <rect x="450" y="64" width="50" height="40"/><rect x="500" y="64" width="50" height="40"/><rect x="550" y="64" width="50" height="40"/><rect x="600" y="64" width="50" height="40"/><rect x="650" y="64" width="60" height="40"/>
  </g>
  <rect x="500" y="64" width="50" height="40" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-size="12"><text x="420" y="82">A 헤더</text><text x="420" y="98" font-family="monospace">free=o1</text>
    <text x="475" y="88">o0</text><text x="525" y="88">o1</text><text x="575" y="88">o2</text><text x="625" y="88">o3</text><text x="680" y="88">…</text></g>
  <path d="M440 104 C 450 130, 500 130, 518 106" stroke="currentColor" fill="none" marker-end="url(#m03b)"/>
  <text x="390" y="150" fill="currentColor" font-size="12">inuse = nobj − 1, o1 의 next=NULL, 두 번째 워드=쿠키</text>
  <g fill="currentColor" font-size="12">
    <text x="20" y="180">① s = p &amp; ~0xFFF → A 헤더, 경계·magic 검사 (slab_lookup)</text>
    <text x="20" y="200">② 쿠키 없음 → 살아 있던 객체 (빠른 경로, free 리스트를 훑지 않음)</text>
    <text x="20" y="220">③ o1 을 free 머리에 push, 쿠키 기록, inuse−−, live−−</text>
    <text x="20" y="240">④ A 가 full 이었으므로 slab_move(c, A, ST_PARTIAL): full 리스트에서 빼서 partial 머리에 (O(1), DS-B06)</text>
  </g>
</svg>
```

**잘못된 해제 7종**의 판정(테스트에서 확인):

| 포인터 | 걸리는 검사 | 결과 |
|---|---|---|
| `NULL` | 첫 줄 | `SF_OK` (C `free(NULL)` 과 같게 무시) |
| 스택 변수 `local` | 아레나 범위 | `SF_BAD_PTR` |
| `a + 1`, `a + 64` (128B 객체 중간) | `off % 128 != 0` | `SF_BAD_PTR` |
| 슬랩 시작 + 8 | `a < obj0` (헤더) | `SF_BAD_PTR` |
| 슬랩 시작 + 64 + 31·128 | `off / 128 >= 31` (꼬리) | `SF_BAD_PTR` |
| 풀에 있는 슬랩의 객체 자리 | `state == ST_POOL` / magic | `SF_BAD_PTR` |
| 이미 해제한 `a` | 쿠키 + `freelist_contains` | `SF_DOUBLE`, 상태 변화 없음 |

사용자 데이터가 우연히 쿠키와 같은 객체 `b` 를 해제하면 느린 확인에서 리스트에 없으므로 정상 해제된다(오탐 없음). 다만 이 검사가 **못 잡는** 경우도 알아 두자: 해제 후 같은 객체가 **재할당된 뒤** 옛 포인터로 다시 free 하면, 그 객체는 지금 살아 있으므로 정상 해제처럼 보인다(use-after-free). 이건 ASan/KASAN 같은 격리(quarantine)·섀도 메모리가 필요한 영역이다.

**랜덤 테스트**: 60000회, 5/8 확률로 임의 클래스 alloc(살아 있는 객체 4000개 상한), 나머지는 임의 객체 free. 할당한 객체는 전체를 무작위 패턴으로 채우고, 해제 직전에 패턴이 그대로인지 본다 — 두 할당이 같은 메모리를 받았다면 여기서 깨진다. NULL 은 `g_pool_n == 0 && n_partial == 0 && n_empty == 0` 일 때만 허용(정확한 "진짜 메모리 부족" 오라클). 처음 4000회는 매번, 이후 32회마다 `slab_check()`. 실제 출력:

```text
[B] random 60000 ops vs model: match (NULL at exhaustion 176, double-free caught 109)
```

살아 있는 객체가 4000개여도 클래스가 섞이고 무작위로 해제되면 슬랩이 듬성듬성해져서 64장이 다 찬다 — NULL 176번은 이 소진 경로가 실제로 시험되었다는 뜻이다.

**복잡도와 결정성**

| 연산 | 시간 | 비고 |
|---|---|---|
| `slab_of` | O(1) | AND 한 번 |
| `kc_alloc` (partial/empty 있음) | O(1) | 리스트 머리, pop |
| `kc_alloc` (새 슬랩) | O(nobj) | `slab_format` 이 최대 252개를 잇는다 — 최악 시간의 주범 |
| `slab_free` (정상) | O(1) | 나눗셈 1~2번(`%`, `/`) + 리스트 이동 |
| `slab_free` (쿠키 의심) | O(nobj) | 이중 해제 확인 경로 |
| `slab_reap` | O(빈 슬랩 수) ≤ 4 | |

메모리 오버헤드: 객체당 0바이트(free 리스트가 객체 안에), 슬랩당 헤더 64B + 꼬리(128B 클래스만 64B) → 1.6–3.1%. 슬랩 포맷의 O(nobj) 를 없애려면 "아직 한 번도 안 나간 객체" 를 bump 인덱스로 내주는 **지연 포맷**을 쓴다(Q6).

### Part C — 내부 단편화 측정과 malloc 비교

센서 허브 파이프라인(100000회, FIFO 큐 최대 1500개, 생산 9 : 소비 7)의 실제 출력:

```text
[C] kind           size  class  count
[C] BLE notify       20     32  10124
[C] IMU sample       24     32  20347
[C] log record       48     64   7634
[C] CAN-FD frame     72    128   7667
[C] NMEA sentence    82    128   4974
[C] internal frag: size classes 34.5% | single 128B pool 68.9% (allocs 50746)
[C] peak: 27 slabs (110592 B) holding 60910 B requested -> utilization 55.1%
```

- **내부 단편화** = (받은 크기 합 − 요청 크기 합) / 받은 크기 합 = 34.5%. 크기 하나짜리 128B 고정 풀(DS-M02)로 했다면 68.9% — 클래스를 나눈 것만으로 절반으로 줄었다.
- 그래도 34.5%는 크다. 원인은 72B → 128B(56B 낭비), 82B → 128B(46B 낭비). 2의 거듭제곱 클래스의 최악 내부 단편화는 `2^k + 1` 바이트 요청일 때로 `(2^k − 1) / 2^(k+1)` → 50%에 가까워진다(65B 요청 → 63/128 = 49.2%).
- **피크 사용률 55.1%** 는 내부 단편화(34.5%)에 슬랩 단위 단편화(partial 슬랩의 빈 칸, 헤더·꼬리)가 더해진 값이다.
- 해결책: ① 자주 쓰는 크기 전용 캐시(`kmem_cache_create("canfd", 72, ...)` 처럼) — 72B 전용이면 슬랩당 56개, 꼬리 0B(연습 3). ② 클래스를 촘촘히(Linux 는 `kmalloc-96`, `kmalloc-192` 를 추가로 둔다).

**demo_bench.c** — 같은 할당기(`solution.c` 를 `main` 이름만 바꿔 포함)를 시스템 `malloc` 과 비교한다. 이 Mac(Apple Silicon, -O2) 실측, 5회 중 최솟값. 절대 ns 보다 **비율**을 보자.

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
```

```text
speed (ns per alloc+free pair, or per op for mixed; best of 5)
  pattern                 slab    malloc   malloc/slab
  LIFO pair 48B           3.91     5.79     1.5x
  batch 3000x64B FIFO     3.91     8.09     2.1x
  mixed 1..128B (op)      9.32    10.19     1.1x   (slab NULLs 0)
  arena intact after bench: yes

internal fragmentation per request size
  req   slab class   malloc_size
     1           16            16
     8           16            16
    16           16            16
    17           32            32
    20           32            32
    24           32            32
    33           64            48
    48           64            48
    65          128            80
    72          128            80
    82          128            96
   100          128           112
   128          128           128
  message mix: slab 34.5% waste, malloc 17.1% waste
```

(다시 돌리면 3.7–3.9 / 5.6–5.8 / 3.7–3.9 / 7.9–8.4 / 9.0–9.3 / 9.9–10.2 ns 범위로 비율은 같았다.)

해석 — 정직하게:

- 속도는 1.1–2.2배. macOS 의 작은 객체 `malloc` 은 이미 크기 클래스·스레드별 캐시를 가진 매우 빠른 할당기라서 "malloc 은 느리다" 는 말은 여기선 반만 맞다. 우리 쪽이 빠른 이유는 락·원자 연산이 없고(단일 스레드 전용) 크기 계산이 단순해서다. 랜덤 혼합에서 차이가 거의 없는 건 두 쪽 모두 캐시 미스·분기 예측 실패가 시간을 지배하기 때문이다. 우리 free 경로에는 런타임 크기의 `%`, `/` 가 있다 — 2의 거듭제곱 클래스니 `& (size − 1)`, `>> shift` 로 바꾸거나 역수 곱(연습 3)을 쓰면 더 줄일 수 있다.
- **단편화는 malloc 이 이겼다**(17.1% vs 34.5%). 출력에서 보이듯 macOS malloc 은 작은 크기를 16바이트 단위로 올림한다(33→48, 72→80). 2의 거듭제곱 클래스 4개는 단순함과 맞바꾼 낭비다.
- 펌웨어에서 슬랩을 쓰는 진짜 이유는 평균 속도가 아니라: ① **힙 없이 정적 메모리**만으로 동작(링커 맵에 256KB 가 그대로 보인다), ② **상한이 있는 시간**(포맷 경로를 지연 포맷으로 바꾸면 alloc/free 모두 O(1)), ③ 크기별로 격리되어 한 종류의 폭주가 다른 종류의 단편화를 만들지 않음, ④ 통계(`live`, 리스트 길이)가 그대로 진단 정보가 된다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 40/40 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o solution_asan && ./solution_asan   # 깨끗, 40/40
```

ASan 은 우리 아레나를 "하나의 정적 배열"로만 보므로 객체 경계 넘침은 못 잡는다. 객체 단위로 잡으려면 해제 시 `ASAN_POISON_MEMORY_REGION` 으로 독을 칠한다(DS-M02 참고).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 아레나를 그냥 `static uint8_t arena[256*1024]` 로 선언 | 시작이 4KB 배수가 아니면 `p & ~0xFFF` 가 이웃 슬랩 중간이나 아레나 밖을 가리킨다 | `static _Alignas(4096) slab_page_t g_arena[64]` (MCU 면 링커 스크립트 `ALIGN(4096)` 도 가능) |
| `if (p < g_arena || p >= g_arena + N)` 로 범위 검사 | 서로 다른 객체를 가리키는 포인터의 관계 비교는 UB(C11 6.5.8p5) — 아레나 밖 포인터가 정확히 그 경우 | `uintptr_t` 로 바꿔 정수 비교 |
| `*(void **)obj = next;` | 선언 타입이 있는 저장소를 다른 타입으로 접근 — strict aliasing 위반, -O2 에서 재배치될 수 있다 | `memcpy(obj, &next, sizeof next)` |
| free 후 full → partial 이동 누락 | full 리스트의 슬랩에 free 객체가 생겼는데 kc_alloc 은 partial 만 본다 → 사실상 누수, 불변식 2 위반 | `else if (s->state == ST_FULL) slab_move(c, s, ST_PARTIAL)` |
| 빈 슬랩을 바로 풀로 반납 | 경계에서 alloc/free 가 번갈아 오면 매번 252개 포맷 — 진동 | `EMPTY_KEEP` 개는 empty 리스트에 남기고, 메모리 압박 때 `slab_reap()` |
| kc_alloc 에서 empty 를 partial 보다 먼저 사용 | 객체가 여러 슬랩에 흩어져 어느 슬랩도 비지 않는다 → 다른 클래스로 회수 불가 | partial → empty → 풀 순서 |
| `slab_lookup` 에서 헤더·꼬리 영역을 안 거름 | `slab + 8` 을 free 하면 헤더 안에 next 를 써서 헤더 파괴 | `a < obj0` 와 `off / size >= nobj` 검사 |
| 클래스 최소 크기 < 16B | 객체에 next(8B) + 쿠키(8B) 가 안 들어간다 | 최소 클래스 16B, 8B 클래스가 필요하면 쿠키 대신 슬랩별 비트맵(DS-B08) |
| `size_to_class(0)` 이 0 을 반환 | malloc(0) 의 동작은 구현 정의(C11 7.22.3p1: NULL 또는 고유 포인터). 정책을 정하지 않으면 0바이트 할당이 16B 를 먹는다 | 이 레슨은 NULL. Linux `kmalloc(0)` 은 `ZERO_SIZE_PTR` 을 돌려준다 |
| CLZ 버전에서 `__builtin_clz(n - 1)` 을 n=1 에 호출 | 인자 0 이면 UB (Cortex-M 의 `CLZ` 명령은 32를 주지만 컴파일러는 그걸 가정하지 않는다) | n ≤ 16 을 먼저 거른다 (연습 1) |

## 8. 면접 Q&A

<details>
<summary>Q1. free(p) 는 크기를 안 받는데 어떻게 어느 클래스인지 아나요? 마스킹이 안 되는 경우는?</summary>

슬랩이 2^k 크기·2^k 정렬이면 `p & ~(2^k − 1)` 이 슬랩 헤더이고 헤더에 클래스가 있다. 성립 조건은 슬랩 정렬, 객체가 슬랩 경계를 넘지 않음, 헤더가 슬랩 안에 있음. 객체가 슬랩보다 크거나 슬랩이 여러 페이지면 마스크로 시작을 못 찾는다 → **슬랩 밖 메타데이터**: `meta[(p − base) >> PAGE_SHIFT]` (연습 2). Linux 가 이 방식이다 — 페이지마다 `struct page` 배열이 있고 `kfree()` 는 `virt_to_folio()` 로 주소에서 페이지 메타데이터를 찾은 뒤 슬랩이면 그 캐시로, 아니면 큰 kmalloc(페이지 할당) 해제로 보낸다. 꼬리질문 "헤더를 슬랩 안에 두면 단점?" → 객체 수 감소(252 vs 256), 사용자 버퍼 언더플로가 헤더를 바로 덮어쓴다.

</details>

<details>
<summary>Q2. 고정 크기 풀(DS-M02), 슬랩, 버디(DS-M04), TLSF(DS-A06) — 언제 무엇을?</summary>

고정 풀: 크기 하나, O(1), 가장 단순 — 패킷 버퍼처럼 크기가 정해진 경우. 슬랩: 고정 풀 여러 개 + 슬랩 단위로 클래스 간 메모리 재배분 — 작은 객체가 여러 크기일 때. 버디: 2^k 페이지 단위 큰 블록, 분할·병합으로 외부 단편화 관리 — Linux 는 버디(페이지) 위에 슬랩(객체)을 올린 2층 구조다. TLSF: 임의 크기에 O(1) 최악 시간 보장, 인접 블록 병합 — 실시간 범용 힙(RTOS 의 malloc 대체). 이 레슨 슬랩은 전역 풀이 고정 64장이라 버디 역할을 "4KB 고정 페이지 스택"으로 단순화한 것이다.

</details>

<details>
<summary>Q3. 내부 단편화가 34.5% 나왔습니다. 어떻게 줄이나요?</summary>

① 클래스 간격을 촘촘히: 2의 거듭제곱이면 최악 ~50%, jemalloc 처럼 2배 구간마다 클래스 4개를 두면(예: 128, 160, 192, 224, 256) 그 구간의 최악 낭비가 1/5 미만이 된다(129B → 160B). 아주 작은 크기는 16B 단위 간격이라 이 비율이 적용되지 않는다. Linux kmalloc 은 2의 거듭제곱 외에 96, 192 클래스를 둔다. ② 많이 쓰는 객체는 전용 캐시(`kmem_cache_create`) — 72B CAN-FD 프레임 전용이면 슬랩당 56개, 낭비 0(연습 3). ③ 측정 먼저: 실제 트레이스로 크기 히스토그램을 뽑아 클래스 경계를 정한다. 대가: 클래스가 많을수록 partial 슬랩이 많아져 슬랩 단위 단편화와 메타데이터가 는다. 이 Mac 의 malloc 은 16B 단위 올림이라 같은 트레이스에서 17.1% 였다.

</details>

<details>
<summary>Q4. ISR 과 태스크가 모두 할당한다면? 멀티코어라면?</summary>

단일 코어 MCU: alloc/free 가 짧고 상한이 있으니 인터럽트 금지(또는 BASEPRI 마스킹) 임계 구역으로 감싼다. 상한을 지키려면 새 슬랩 포맷(O(nobj))을 지연 포맷으로 바꾸고 이중 해제 확인 경로는 디버그 빌드에서만. 멀티코어: 전역 락은 경합이 생기므로 **코어별 캐시**를 둔다 — Bonwick 의 magazine(2001), SLUB 의 per-CPU 슬랩/freelist 가 이 아이디어다. 연습 5에서 크기 16 매거진을 앞에 두면 100000 연산 동안 슬랩 계층 진입(=락)이 99999번 → 1225번(81.6배 감소)이었다. 꼬리질문 "다른 코어가 해제하면?" → remote free, OS-M06 에서 다룬다.

</details>

<details>
<summary>Q5. 이중 해제와 메모리 손상은 어떻게 잡나요? 실제 할당기는?</summary>

이 레슨: free 객체 두 번째 워드의 쿠키로 의심 → free 리스트 탐색으로 확정. glibc 2.29+ tcache 가 같은 구조다(해제된 청크의 `key` 필드가 `tcache_key` 와 같으면 해당 tcache bin 을 훑어 확인 후 "free(): double free detected in tcache 2" 로 abort). Linux SLUB 은 `slub_debug=FZPU`(sanity check, red zone, poison, user tracking) 로 객체 앞뒤 red zone 과 해제 객체 poison 을 검사하고, `CONFIG_SLAB_FREELIST_HARDENED` 는 free 리스트 next 포인터를 비밀 값·주소와 XOR 해 덮어쓰기 공격을 어렵게 한다. 더 강하게는 KASAN(섀도 메모리 + 해제 객체 격리). 쿠키 방식의 한계: 재할당 후의 use-after-free 는 못 잡는다.

</details>

<details>
<summary>Q6. 이 구현의 최악 시간은 O(1) 인가요? 아니라면 어떻게 고치나요?</summary>

아니다. 새 슬랩을 꺼낼 때 `slab_format` 이 nobj(최대 252)개를 잇는 O(nobj), 이중 해제 의심 경로가 O(nobj). 고치는 법: 헤더에 `bump`(아직 한 번도 안 나간 첫 객체 인덱스)를 두고 alloc 은 "free 리스트가 있으면 pop, 없으면 `bump++` 위치" — 포맷이 O(1)이 된다. 이중 해제 확인은 슬랩별 할당 비트맵(252비트 = 32B, DS-B08)으로 바꾸면 O(1)이지만 헤더가 커진다(64B 안에 안 들어감 → 헤더 128B 이거나 슬랩 밖으로). 실시간 시스템은 "평균"이 아니라 이런 최악 경로를 질문받는다.

</details>

<details>
<summary>Q7. Bonwick 슬랩 할당기의 원래 아이디어 중 이 레슨에 없는 것은?</summary>

① **객체 캐싱(constructor/destructor)**: 객체를 "초기화된 상태"로 캐시에 보관해 매번 초기화(락 초기화, 리스트 헤드 설정)를 피한다 — 해제는 상태를 원래대로 돌려놓고 반납. ② **캐시 컬러링(coloring)**: 슬랩마다 객체 시작 오프셋을 꼬리 낭비 범위 안에서 조금씩 달리해, 여러 슬랩의 같은 인덱스 객체가 같은 캐시 세트에 몰리지 않게 한다. 128B 클래스의 64B 꼬리가 바로 컬러링에 쓸 수 있는 여유다. ③ 큰 객체용 off-slab 메타데이터(연습 2). 모두 Bonwick, "The Slab Allocator: An Object-Caching Kernel Memory Allocator" (USENIX Summer 1994) 에 있다.

</details>

<details>
<summary>Q8. 슬랩 할당기에서 외부 단편화는 없나요?</summary>

객체 단위로는 없지만 **슬랩 단위**로 있다. 슬랩마다 객체 하나만 살아 있어도 슬랩 전체가 그 클래스에 묶인다(pinning). 연습 4에서 16B 객체 16개(256B)가 슬랩 16장(64KB)을 붙잡아 사용률 0.39%, 그 상태에서 `alloc(128)` 은 NULL 이었다. C 에서는 객체를 옮길 수 없으므로(포인터가 밖에 있다) 압축으로 못 푼다. 대응: 수명이 비슷한 객체끼리 같은 캐시/아레나에(수명 긴 설정 객체와 짧은 메시지를 섞지 않기), partial 중 가장 꽉 찬 슬랩 우선 할당, 부팅 때 할당해 놓고 영원히 쓰는 객체는 아예 정적 배열로.

</details>

## 9. 추가 연습

### 연습 1 — 크기 클래스: 루프 / CLZ / 표

`size_to_class` 를 `__builtin_clz` 버전과 8칸 표 버전으로 만들고, n = 0..1024 와 `SIZE_MAX` 에서 루프 버전과 같은지 전수 검사하라.

<details>
<summary>답 보기</summary>

CLZ: `n ≤ 16 → 0`, 그 외 `(32 − clz(n − 1)) − 4`. n−1 의 최상위 비트 위치 + 1 이 `ceil(log2 n)` 이다(n 이 2의 거듭제곱이면 n−1 은 한 자리 작다). n ≤ 16 을 먼저 거르는 건 `clz(0)` UB 회피와 클래스 0 하한을 동시에 처리한다. 표: `lut[(n − 1) >> 4] = {0,1,2,2,3,3,3,3}` — 128 이하에서는 16 으로 나눈 칸이 8개뿐이다. Linux 도 작은 크기는 표(`mm/slab_common.c` 의 size index 표), 큰 크기는 `fls(size − 1)` 로 kmalloc 캐시를 고른다. Cortex-M3 이상은 `CLZ` 명령 1개(CMSIS `__CLZ()`), Cortex-M0 에는 없어서 표가 더 빠르다.

</details>

### 연습 2 — 슬랩 밖 메타데이터(side table)

헤더를 슬랩에서 빼고 `meta[(p − base) >> 12]` 로 찾는 미니 할당기를 만들어 클래스별 객체 수를 비교하고, 슬랩 3장의 **모든 바이트 주소**에 대해 조회가 객체 시작에서만 성공하는지 확인하라.

<details>
<summary>답 보기</summary>

```text
[2] class | header-in-slab nobj | side-table nobj
[2]    16 |                 252 |             256
[2]    32 |                 126 |             128
[2]    64 |                  63 |              64
[2]   128 |                  31 |              32
```

헤더 64B 를 빼면 클래스마다 `ceil(64 / size)` 개가 더 들어간다. 조회는 `(p − base) >> 12` 로 슬랩 번호, `(p − base) & 0xFFF` 로 오프셋 — 아레나가 4KB 정렬일 필요가 없다(practice.c 의 아레나는 `_Alignas(16)` 만). 32B 슬랩·128B 슬랩·미사용 슬랩 3장(12288 주소) 중 정확히 128 + 32 = 160 곳에서만 성공했다. 대가: 메타데이터 배열이 따로 필요하다(practice.c 의 `pmeta_t` 는 슬랩당 16B). 덤으로 사용자 버퍼 언더플로가 헤더를 덮는 일이 없어진다.

</details>

### 연습 3 — 임의 크기 전용 캐시와 역수 곱

72B CAN-FD 전용 캐시처럼 2의 거듭제곱이 아닌 크기에서 `off / size` 나눗셈을 `(off × R) >> 32`, `R = ceil(2^32 / size)` 로 바꾸고, 크기 1..4096 × 오프셋 0..4095 전수 검사하라. 메시지 크기별 전용 캐시의 슬랩당 객체 수와 꼬리를 출력하라.

<details>
<summary>답 보기</summary>

`R = 2^32/size + e` (0 ≤ e < 1) 라 `off·R / 2^32 = off/size + off·e/2^32`. `off/size` 의 소수부는 최대 `(size−1)/size` 이므로 오차 `off·e/2^32` 가 `1/size` 미만이면 내림 결과가 같다 — 충분조건 `off < 2^32 / size`. off < 4096, size ≤ 4096 이면 `2^32/4096 = 2^20` 이라 항상 성립하고 전수 검사도 통과했다. size = 1 이면 R = 2^32 라 32비트에 안 들어가므로 R 은 `uint64_t`. SLUB 도 `obj_to_index()` 에서 나눗셈 대신 `reciprocal_divide()` 를 쓴다.

```text
[3] size | nobj | tail B | waste/obj in pow2 class
[3]   20 |  201 |     12 | 12
[3]   24 |  168 |      0 | 8
[3]   48 |   84 |      0 | 16
[3]   72 |   56 |      0 | 56
[3]   82 |   49 |     14 | 46
```

72B 전용 캐시는 4032 = 56 × 72 로 꼬리 0, 128B 클래스에서 객체마다 버리던 56B 가 사라진다.

</details>

### 연습 4 — 슬랩 고정(pinning) 재현

16장짜리 미니 할당기를 16B 객체로 가득 채운 뒤 슬랩마다 하나만 남기고 모두 해제하라. 그 상태에서 128B 할당이 되는가? 사용률은?

<details>
<summary>답 보기</summary>

```text
[4] live 16 objs x 16 B = 256 B pin 16 slabs = 65536 B -> utilization 0.39%; alloc(128) = NULL
```

살아 있는 객체가 256B 뿐인데 64KB 가 16B 클래스에 묶였다. 남은 16개를 해제하면 슬랩이 비어 128B 할당이 성공한다(테스트 확인). 객체를 옮길 수 없는 C 할당기의 근본 한계 — 수명별로 캐시를 나누는 것이 실전 대응이다(Q8).

</details>

### 연습 5 — 매거진: 슬랩 계층 진입 줄이기

크기 16칸 매거진(LIFO 배열)을 슬랩 앞에 두고, 비면 락 한 번에 8개 채우기, 가득 차면 락 한 번에 8개 반납하라. live ≤ 300 인 랜덤 alloc/free 100000회에서 락 진입 횟수를 매거진 유무로 비교하고, 끝나면 모든 슬랩이 반납됐는지 확인하라.

<details>
<summary>답 보기</summary>

```text
[5] slab-layer lock entries for 100000 ops: plain 99999, magazine 1225 (81.6x fewer)
```

alloc/free 가 대부분 매거진 안에서 끝나고(락 없음, 코어 로컬), 매거진이 비거나 찰 때만 묶음으로 슬랩 계층에 간다. 채움(8) < 용량(16) 으로 히스테리시스를 둬야 경계에서 진동하지 않는다. 대가: 매거진에 든 객체는 슬랩 입장에선 "사용 중"이라 빈 슬랩 반납이 늦어진다 → 메모리 압박 시 `mag_drain()`. SLUB per-CPU 슬랩, Bonwick 2001 magazine 계층과 같은 원리다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 11/11 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o practice_asan && ./practice_asan   # 깨끗
```

## 10. 핵심 정리

- 슬랩 할당기 = 크기 클래스별 고정 풀 + 슬랩(4KB) 단위로 클래스 간 메모리 재배분. 할당기 안에 malloc 없음, 정적 4KB 정렬 아레나.
- `p & ~0xFFF` → 슬랩 헤더 → 클래스. 그래서 `free(p)` 에 크기가 필요 없다. 조건: 2^k 크기, 2^k 정렬, 객체가 경계를 안 넘음. 안 되면 side table(`meta[(p − base) >> 12]`, Linux `struct page`).
- free 객체 리스트는 객체 안에(객체당 오버헤드 0), LIFO 라 캐시에 따뜻하다. `memcpy` 로 읽고 써서 aliasing 회피.
- 캐시 상태 기계: alloc 은 partial → empty → 풀, free 는 full→partial, 0개→empty(1개까지)/풀. partial 우선 = 슬랩을 비우기 위해.
- 불변식: 모든 슬랩은 정확히 한 리스트, 리스트 = inuse 범위, free 리스트 길이 = nobj − inuse. 매 연산 검사 + 패턴 채운 참조 모델.
- 이중 해제 = 쿠키(빠른 의심) + 리스트 탐색(확정), glibc tcache `key` 와 같은 구조. 재할당 후 UAF 는 못 잡는다.
- 실측: 2의 거듭제곱 클래스 내부 단편화 34.5%(128B 단일 풀 68.9%, macOS malloc 17.1%), 속도는 malloc 대비 1.1–2.2배. 펌웨어에서의 가치는 정적 메모리·상한 시간·격리.
- 최악 시간 O(1) 로 만들려면 지연(bump) 포맷 + 디버그 전용 이중 해제 확인. 멀티코어/ISR 은 per-CPU 매거진.

## 참고자료

- Jeff Bonwick, "The Slab Allocator: An Object-Caching Kernel Memory Allocator", USENIX Summer 1994
- Jeff Bonwick, Jonathan Adams, "Magazines and Vmem: Extending the Slab Allocator to Many CPUs and Arbitrary Resources", USENIX Annual Technical Conference 2001
- [Linux `mm/slub.c` — SLUB 할당기](https://github.com/torvalds/linux/blob/master/mm/slub.c), [`mm/slab.h` — `struct slab`, `obj_to_index()`](https://github.com/torvalds/linux/blob/master/mm/slab.h), [`mm/slab_common.c` — kmalloc 캐시 선택](https://github.com/torvalds/linux/blob/master/mm/slab_common.c)
- [Linux 문서: Short users guide for SLUB (`slub_debug`)](https://docs.kernel.org/mm/slub.html)
- [glibc `malloc/malloc.c` — tcache 이중 해제 검출(`key`)](https://sourceware.org/git/?p=glibc.git;a=blob;f=malloc/malloc.c)
- [jemalloc 매뉴얼 — size classes](https://jemalloc.net/jemalloc.3.html)
- ISO/IEC 9899:2011 (C11) 6.5p7 effective type, 6.5.8p5 포인터 관계 비교, 7.22.3p1 크기 0 할당
- 관련 레슨: DS-B06(sentinel 리스트), DS-B08(비트맵), DS-M02(고정 크기 풀), DS-M04(버디), DS-A06(TLSF), OS-M05/M06(see:OS)
