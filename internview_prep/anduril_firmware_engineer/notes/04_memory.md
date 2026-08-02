# 04. 메모리 & 포인터 (Memory & Pointers) — Q41–55 🧠

> Anduril 펌웨어 인터뷰의 **가장 기초이자 가장 자주 파고드는** 영역.
> `mem*` 계열 직접 구현, 포인터 조작, 그리고 `const`/`volatile`/함수 포인터/
> 정적 할당기 같은 **임베디드 C 정통 개념**을 "돌아가는 데모 + 정확한 설명"으로 정리한다.
>
> 빌드: `cc -std=c11 -Wall -Wextra solutions/04_memory.c -o /tmp/andb_memory && /tmp/andb_memory`

---

## 왜 이 주제가 Anduril에서 중요한가

- 드론/자율 시스템 펌웨어는 **힙을 거의 안 쓰고** 고정 버퍼 위에서 돈다 → `memcpy/memmove`,
  정적 풀 할당기, 정렬(alignment)이 실무 그 자체.
- **DMA 버퍼, 이중 버퍼(ping-pong), 패킷 재조립**에서 `memmove` 겹침 처리와 정렬은 단골.
- **메모리맵 레지스터/ISR 공유 플래그**는 `volatile` 없이는 최적화로 사라진다 → 버그.
- 명령 디코더/프로토콜 핸들러는 **함수 포인터 디스패치 테이블**로 짠다.
- 면접관은 "동작"에서 멈추지 않고 **왜 unsigned로 비교? 왜 뒤에서부터 복사? 패딩은 몇 바이트?**
  를 계속 파고든다. 근거를 말로 설명할 수 있어야 한다.

---

## mem* 계열 — 구현 핵심

| 함수 | 한 줄 요약 | 반드시 짚을 것 |
|------|-----------|---------------|
| `my_memset` | n바이트를 `(unsigned char)c`로 채움 | `c`는 하위 1바이트만. `void*`→`unsigned char*` 캐스팅 |
| `my_memcpy` | 겹침 **미보장** 복사 | `restrict`로 non-overlap 계약. dest 반환 |
| `my_memmove` | 겹쳐도 안전 | **dest>src면 뒤에서부터**, dest<src면 앞에서부터 |
| `my_memcmp` | 첫 불일치 부호 | **반드시 unsigned char로 비교** (0x80 vs 0x01) |
| `my_memchr` | 첫 바이트 위치 | 못 찾으면 NULL. const 벗겨 반환 |

### memmove 방향 결정 — 이게 핵심 함정

```c
void *my_memmove(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    if (d == s || n == 0) return dest;
    if (d < s) {                       // 앞→뒤: dest가 src보다 앞이면 안전
        for (size_t i = 0; i < n; ++i) d[i] = s[i];
    } else {                           // 뒤→앞: dest가 src 뒤와 겹칠 때 원본 보존
        for (size_t i = n; i > 0; --i) d[i-1] = s[i-1];
    }
    return dest;
}
```

- `memcpy`로 겹친 영역을 복사하면 **UB**. 실제로 뒤쪽이 먼저 덮여 원본이 깨진다.
- "왜 memcpy는 restrict인가?" → 컴파일러가 겹침 없다고 가정하고 벡터화/재정렬 최적화를 하기 때문.

### memcmp 부호 함정

```c
return (int)a[i] - (int)b[i];   // a,b는 unsigned char*. signed char면 0x80이 음수가 됨
```

---

## 개념 문제 — 데모로 이해하기

### 48. `const` 위치별 의미 (오른쪽→왼쪽으로 읽기)

```c
const int *p;        // p가 가리키는 int는 읽기전용. p는 재지정 가능.   (pointer-to-const)
int * const p;       // p 자체가 const. *p는 수정 가능, p 재지정 불가.  (const pointer)
const int * const p; // 둘 다 불가.
```

- 읽는 법: `const` 왼쪽에 타입이 있으면 그 값이 const, `*` 오른쪽에 `const`면 포인터가 const.
- 임베디드에서 `const volatile uint32_t *` = **읽기전용 상태 레지스터**(내가 못 쓰지만 HW가 바꿈).

### 49. `volatile` — HW 레지스터 / ISR / 폴링

```c
bool wait_ready(volatile const uint32_t *status, uint32_t mask, int max_iters) {
    for (int i = 0; i < max_iters; ++i)
        if (*status & mask) return true;   // volatile: 매 반복 실제 메모리 재로드
    return false;
}
```

- `volatile` 없으면 컴파일러가 `*status`를 **한 번만 읽고 레지스터에 캐싱** → 무한 루프 or 값 안 바뀜.
- 쓰는 곳: ① 메모리맵 주변장치 레지스터, ② ISR과 main이 공유하는 플래그, ③ 하드웨어가 바꾸는 메모리.
- ⚠️ `volatile`은 **원자성/메모리 순서를 보장하지 않는다**. 멀티코어 동기화는 atomic/배리어가 필요.
  (single-writer ISR 플래그 정도에만 안전.)

### 50. 함수 포인터 디스패치 테이블

```c
typedef int32_t (*alu_fn_t)(int32_t, int32_t);
int32_t dispatch(uint8_t opcode, int32_t a, int32_t b) {
    static const alu_fn_t table[] = { op_add, op_sub, op_mul, op_and };
    if (opcode >= sizeof table / sizeof table[0]) return INT32_MIN;  // 범위 방어
    return table[opcode](a, b);
}
```

- `switch`보다 확장성 좋고 O(1) 분기. 명령 디코더/프로토콜 커맨드 핸들러 패턴.
- **반드시 인덱스 범위 체크** — 안 하면 임의 코드 실행/크래시.

### 51. 정적 메모리 풀 malloc/free

- 아이디어: `static uint8_t pool[N]` 위에 **블록 헤더(size, used, next)** + first-fit + split + free 시 인접 병합(coalesce).
- 왜? 임베디드는 힙 파편화/비결정성을 싫어함. 정적 풀은 **결정적**이고 컴파일 타임에 크기 확정.
- 함정: **정렬**(payload를 8/16바이트 경계로), split 최소 블록 크기, free 시 coalesce 안 하면 파편화.

### 52. Dangling pointer (허상 포인터)

```c
void safe_free(void **pp) {       // free 후 즉시 NULL 로 → 이후 사용/이중free 차단
    if (!pp || !*pp) return;
    free(*pp);
    *pp = NULL;
}
```

- 원인: ① `free` 후 그 포인터 사용, ② **지역변수 주소 반환**, ③ 이중 free.
- 방어: free 즉시 NULL, 소유권 명확히, 지역변수 주소 절대 반환 금지.

### 53. `void*` — 타입 소거(generic)

```c
void generic_swap(void *a, void *b, size_t size) {
    unsigned char *pa = a, *pb = b;
    for (size_t i = 0; i < size; ++i) { unsigned char t = pa[i]; pa[i] = pb[i]; pb[i] = t; }
}
```

- `void*`는 타입 정보를 버린 "주소만". 크기를 따로 넘겨줘야 함(`qsort` 시그니처가 그 예).
- 안전 규칙: **정확한 원래 타입으로만 다시 캐스팅**. `void*` 산술은 표준 C에서 불가(GCC 확장뿐).

### 54. 구조체 패딩 / 정렬

```c
struct Unpacked  { uint8_t a; uint32_t b; uint8_t c; }; // 1 +3pad +4 +1 +3pad = 12
struct Reordered { uint32_t b; uint8_t a; uint8_t c; }; // 4 +1 +1 +2pad       = 8
```

- 각 멤버는 **자기 크기(정렬 요구)의 배수 주소**에 놓임 → 사이/끝에 padding.
- 구조체 전체 크기는 **가장 큰 멤버 정렬의 배수**(배열로 놓을 때 정렬 유지).
- 최적화: **큰 멤버부터 배치**하면 패딩 최소. 와이어 포맷은 `#pragma pack`/직렬화로 강제(단, 정렬 안 된 접근은 일부 MCU에서 fault).

### 55. 포인터 배열 vs 배열 포인터

```c
int *pa[4];      // 포인터 4개를 원소로 갖는 "배열"        (array of pointers)
int (*pp)[4];    // "int[4] 배열 전체"를 가리키는 "포인터"  (pointer to array)
```

- `int (*pp)[4] = &arr;` 이면 `(*pp)[i]`로 접근. `pp+1`은 **16바이트**(int[4] 하나) 점프.
- 문자열 테이블(`char *argv[]`)이 대표적 포인터 배열. 2D 배열 파라미터 전달엔 배열 포인터.

---

## 인터뷰 팔로업 (실제로 자주 나오는 것)

- "`memcpy`와 `memmove` 차이? 언제 어느 걸?" → 겹침 가능성 있으면 무조건 `memmove`.
- "`memcpy`를 워드 단위로 최적화하려면?" → 정렬 확인 후 `uint32_t/uint64_t` 단위 복사 + 잔여 바이트 처리.
- "`volatile`이 스레드 동기화에 충분한가?" → **아니다.** atomic/뮤텍스/배리어 필요.
- "이 구조체 `sizeof`는?" → 멤버 정렬 규칙으로 손으로 계산 시연.
- "정적 할당기에서 파편화를 어떻게 줄이나?" → coalesce, size class, 고정 블록 풀.
- "dangling을 어떻게 예방?" → free 후 NULL, 소유권 규칙, 지역 주소 반환 금지, sanitizer(ASan).

## 자주 나오는 버그 / 함정 체크리스트

- [ ] `memcmp`를 `signed char`로 비교 (부호 뒤집힘)
- [ ] `memmove`에서 방향 안 나눔 (겹침 시 데이터 손상)
- [ ] `memset`의 `c`를 int 전체로 씀 (하위 1바이트만 유효)
- [ ] 디스패치 테이블 **인덱스 범위 미검사**
- [ ] ISR 공유 플래그에 `volatile` 누락
- [ ] free 후 포인터 재사용 / 이중 free
- [ ] 구조체 패딩을 무시하고 `sizeof` 대신 멤버 합으로 계산
- [ ] `n == 0` / `NULL` 인자 미방어
