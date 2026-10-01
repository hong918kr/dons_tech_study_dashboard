# 08. 엔디언과 정렬 — byte swap, unaligned 접근, align_up

> **주제**: 엔디언 · 정렬 · strict aliasing · **난이도**: 중급 · **목표 시간**: 30분
> **레벨**: L0 비트·정수 기초 (06 → 07 → 08 → 09)
> **해설은 보지 말 것**: 먼저 `starters/08_endian_align.c`를 채워 `make run N=08`으로 통과시킨다.

## 면접관의 문장

"Here is a byte buffer that came off a UART. The protocol says the fields are big-endian, and one of the 32-bit fields sits at offset 3. Parse it. I have seen candidates write `*(uint32_t *)(p + 3)` here, so tell me what is wrong with that on our target and what the compiler is allowed to do with it at `-O2`. Then give me the byte-swap helpers for 16, 32 and 64 bits, a runtime endianness check, and the alignment helpers a small allocator needs: round a size up to a power-of-two boundary, and check whether a pointer is aligned. No division anywhere."

## 요구사항

1. `bswap16` / `bswap32` / `bswap64`는 바이트 순서를 뒤집는다. 두 번 적용하면 항등이다. `uint16_t`가 연산 중 `int`로 승격되는 것을 고려해 결과를 명시적으로 좁힌다.
2. `bswap64`는 32비트 두 번으로 나눠 구현한다(상수 길이와 가독성 때문).
3. `host_endian()`은 런타임에 `ENDIAN_LITTLE` / `ENDIAN_BIG`을 판정한다. 포인터 캐스트가 아니라 `memcpy`로 바이트를 꺼낸다.
4. `load_be16` / `load_be32` / `store_be32` / `load_le32` / `store_le32`는 바이트를 **직접 조립**한다. 그래서 (a) 주소 정렬을 전혀 요구하지 않고 (b) host 엔디언과 무관하게 항상 같은 결과를 준다.
5. `load32_native` / `store32_native`는 host 바이트 순서 그대로, 정렬 요구 없이 읽고 쓴다. `memcpy`를 쓴다. 이 함수를 와이어 포맷에 쓰면 안 되는 이유를 말할 수 있어야 한다.
6. 어느 함수에서도 `uint32_t` 포인터 캐스트를 쓰지 않는다. 정렬 위반과 strict aliasing 위반 두 가지를 동시에 피해야 한다.
7. `is_pow2_size(a)`는 `a`가 0이 아닌 2의 거듭제곱인가를 판정한다. 아래 정렬 함수들은 모두 이것을 `assert`로 전제한다.
8. `align_up(v, a)`는 `v`를 `a`의 배수로 올린다. 이미 배수면 그대로. **나눗셈을 쓰지 않는다**(Cortex-M0에는 하드웨어 나눗셈이 없다). 올림 자체가 `SIZE_MAX`를 넘는 경우를 `assert`로 막는다.
9. `align_down(v, a)`는 내림, `is_aligned(v, a)`는 배수 판정이다.
10. 불변식: `align_down(v,a) <= v <= align_up(v,a)`, 둘 다 `a`의 배수, `align_up(v,a) - v < a`, 그리고 `v`가 이미 정렬되어 있으면 셋이 모두 같다.
11. `ptr_is_aligned(p, a)`와 `ptr_align_up(p, a)`는 `uintptr_t`를 거친다. 포인터 값을 정수로 보는 다른 방법은 표준이 보장하지 않는다.
12. `a == 1`은 항등이어야 한다. `align_up(v, 1) == v`, `ptr_is_aligned(p, 1) == true`.
13. 테스트는 `_Alignas(8)` 버퍼의 offset 0..4에서 전부 통과해야 한다. 즉 정렬이 어긋난 주소에서도 동작해야 한다.
14. 뼈대의 `bump_alloc`과 `wire_parse`, 테스트는 수정하지 않는다. 그 둘은 위 원시 함수를 어떻게 쓰는지 보여 주는 예시다.

## 인터페이스

```c
typedef enum { ENDIAN_LITTLE, ENDIAN_BIG } endian_t;

static uint16_t bswap16(uint16_t v);
static uint32_t bswap32(uint32_t v);
static uint64_t bswap64(uint64_t v);
static endian_t host_endian(void);

static uint16_t load_be16(const void *p);
static uint32_t load_be32(const void *p);
static void     store_be32(void *p, uint32_t v);
static uint32_t load_le32(const void *p);
static void     store_le32(void *p, uint32_t v);
static uint32_t load32_native(const void *p);
static void     store32_native(void *p, uint32_t v);

static bool   is_pow2_size(size_t a);
static size_t align_up(size_t v, size_t a);
static size_t align_down(size_t v, size_t a);
static bool   is_aligned(size_t v, size_t a);
static bool   ptr_is_aligned(const void *p, size_t a);
static void  *ptr_align_up(void *p, size_t a);
```

## 제약

- 동적 할당 금지. 표준 라이브러리는 `assert.h` / `stdbool.h` / `stddef.h` / `stdint.h` / `stdio.h` / `string.h`만.
- `cc -std=c11 -Wall -Wextra -O2 -g`에서 경고 0개.
- 나눗셈(`/`, `%`) 금지.
- `packed` 속성, 컴파일러 확장, `__builtin_bswap*` 금지.
- 버퍼 범위를 벗어난 포인터를 **만들지도** 않는다(계산만 해도 UB다). 경계 검사는 오프셋 정수로 한다.

## 예시 동작

```text
와이어 포맷 (big-endian). seq 는 offset 3 = 절대 4바이트 정렬이 아니다.

  offset 0    1        3           7         9
        +-----+--------+-----------+---------+---------------+
        | ver | len    | seq       | crc     | payload ...   |
        +-----+--------+-----------+---------+---------------+
          u8    u16 BE   u32 BE      u16 BE

  (uint32_t *)(p + 3) 은 두 가지를 동시에 위반한다:
    1. 정렬   — Cortex-M0/M3 에서는 HardFault, 일부 DSP 에서는 조용히 오답
    2. aliasing — 컴파일러가 "p 와 겹치지 않는다"고 가정해 -O2 에서 재배치한다
```

```text
load_be32(p) 는 바이트를 직접 조립한다

  p[0]=0xDE  p[1]=0xAD  p[2]=0xBE  p[3]=0xEF
  (0xDE << 24) | (0xAD << 16) | (0xBE << 8) | 0xEF = 0xDEADBEEF

  같은 4바이트를 load_le32 로 읽으면 0xEFBEADDE 다.
  둘의 관계: load_be32(p) == bswap32(load_le32(p))
```

```text
align_up(13, 8)

  13      = 0000 1101
  + (8-1) = 0001 0100  (= 20)
  & ~7    = 0001 0000  (= 16)     하위 3비트를 잘라 내림

  나눗셈 버전 (v + a - 1) / a * a 는 나눗셈 두 번 = M0 에서 수십 사이클
```

```text
bump 할당자: 오프셋으로 경계 검사한다 (포인터 비교가 아니라)

  arena[64], used = 0
  alloc(1, 1)  -> start = 0,  used = 1     반환 arena + 0
  alloc(4, 4)  -> start = 4,  used = 8     반환 arena + 4   (1 -> 4 로 올림)
  alloc(8, 8)  -> start = 8,  used = 16    반환 arena + 8
  alloc(64, 8) -> n > size - start  ->  NULL,  used 는 16 그대로
```

## 스스로 점검할 질문

1. `*(uint32_t *)(p + 3)`이 x86 host에서는 통과하고 타깃에서만 깨지는 이유는 무엇인가. 어떤 타깃이 그런가.
2. strict aliasing 위반이 "잘못된 주소를 읽는다"가 아니라 "컴파일러가 코드를 재배치한다"로 나타나는 이유는 무엇인가.
3. `memcpy(&v, p, 4)`가 `-O2`에서 함수 호출로 남지 않는 이유는 무엇인가. 남는 경우는?
4. `union`으로 타입 펀닝을 하면 정렬과 aliasing 중 무엇이 해결되고 무엇이 안 되나.
5. `host_endian()`을 컴파일 타임에 알 수 있다면 왜 런타임 판별을 쓰나. 반대로 런타임 판별이 위험한 경우는?
6. `align_up`에서 `v + a - 1`이 넘치면 무슨 값이 나오나. 왜 `assert`가 필요한가.
7. `(uintptr_t)p`로 주소를 정수로 보는 것이 표준에서 어디까지 보장되나.
8. 구조체를 그대로 UART로 보내면 안 되는 이유를 세 가지 대라(엔디언 말고도 있다).
9. `__attribute__((packed))`로 정렬 문제를 없애면 무엇을 대가로 내나.

## follow-up (면접관이 이어서 물을 것)

1. "Show me the assembly `load_be32` turns into on ARM. Is there a `REV` in there?"
2. "The DMA engine needs 32-byte aligned buffers. How do you guarantee that for a statically declared array?"
3. "Your struct has a `uint32_t` after a `uint8_t`. What does the compiler do, and how do you find out portably?"
4. "Now the same parser has to run on a big-endian MIPS box. What in your code changes?"
5. "A colleague wants `#pragma pack(1)` on the whole protocol header. Talk them out of it, or don't."

---

**해설**: [notes/L0_bit_basics.md](../drills/L0_bit_basics.html) §7, §8 · **답안**: `solutions/08_endian_align.c`
