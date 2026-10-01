# L0. 비트·정수 기초 — 개념 레퍼런스

> **목표**: 32비트 워드 하나를 비트 단위로 안전하게 다루는 근육. 마스크·시프트·엔디언·정렬·오버플로에서 나오는 UB를 전부 이름으로 부를 수 있게 한다.
> **수록 문제**: 06 bit_ops · 07 bit_count_reverse · 08 endian_align · 09 saturating_math
> **언제 다시 보나**: 면접 3일 전 워밍업, 드라이버에서 `<<` 나 `memcpy` 를 쓸 때, "왜 x86에서는 되는데 타깃에서 깨지나"를 만날 때.
> **다 읽으면**: (1) 시프트·정수 승격·부호 변환에서 UB가 생기는 지점을 짚는다. (2) 바이트 스트림에서 정수를 꺼내는 올바른 방법과 그 이유를 말한다. (3) 랩어라운드와 포화의 차이를 오디오 파형으로 설명한다.

---

## 0. 왜 이게 면접에 나오나

임베디드 면접의 첫 20분은 거의 항상 비트다. 이유는 셋이다.

**모든 레지스터가 비트 필드다.** 보드 하나 올리려면 클럭 게이트 한 비트, 핀 mux 두 비트, 분주비 12비트를 건드린다. 비트 조작이 안 되면 BSP를 한 줄도 못 쓴다.

**UB를 아는지가 드러난다.** `1 << 31`, `x >> 32`, `a + b > INT_MAX`는 전부 하루 쓰면 통과하고 반년 뒤 최적화 레벨을 올리면 터지는 코드다. 면접관은 이 셋 중 하나를 일부러 유도한다. 그리고 **호스트와 타깃이 다르다는 감각을 본다.** x86-64에서 잘 도는 파서가 Cortex-M0에서 HardFault를 내는 이유(정렬), `long`이 32비트인지 64비트인지, 음수 시프트가 무엇인지. 이걸 모르면 "호스트에서 테스트했습니다"가 안전하다고 착각한다. L0의 네 문제는 이 셋을 순서대로 훑는다.

---

## 1. 32비트 워드를 보는 법

비트 번호는 **LSB = bit 0**. 그림은 항상 왼쪽이 상위다.

```text
  MSB (상위)                                   LSB (하위)
  +--+--+--+--+ ... +--+--+--+--+--+--+--+--+--+--+
  |31|30|29|28|     | 9| 8| 7| 6| 5| 4| 3| 2| 1| 0|
  +--+--+--+--+ ... +--+--+--+--+--+--+--+--+--+--+

 0x12345678 = 0001 0010 0011 0100 0101 0110 0111 1000
```

이 노트의 모든 코드는 세 규칙만 지킨다. (1) 시프트 카운트는 항상 `0 <= n < 32`이고 `n == 32`는 UB다. (2) 왼쪽으로 미는 대상은 항상 unsigned다. (3) 폭 32 마스크는 시프트가 아니라 `0xFFFFFFFFu`를 깎아서 만든다.

---

## 2. 마스크 만들기 (문제 06)

```c
static uint32_t bit_mask(unsigned n)
{
    assert(n < 32u);
    return (uint32_t)1u << n;                    /* 1u 가 핵심 */
}

static uint32_t mask_range(unsigned hi, unsigned lo)
{
    assert(hi < 32u && lo <= hi);
    return (0xFFFFFFFFu >> (31u - hi)) & (0xFFFFFFFFu << lo);
}
```

```text
mask_range(15, 8)

  0xFFFFFFFF >> (31-15) = 0x0000FFFF     위쪽을 깎는다
  0xFFFFFFFF << 8       = 0xFFFFFF00     아래쪽을 밀어낸다
  AND                   = 0x0000FF00

  31                 16 15          8 7           0
 +---------------------+-------------+------------+
 | 0000 0000 0000 0000 | 1111 1111   | 0000 0000  |
 +---------------------+-------------+------------+
```

`1 << 31`이 왜 UB인가. `1`은 `int`다. signed 왼쪽 시프트는 결과가 그 타입에 표현 가능해야 하는데 `1 << 31`은 32비트 `int` 범위를 넘는다. gcc는 대부분 원하는 값을 주지만 UBSan이 잡고 최적화 결정에 영향을 준다. `1UL << n`도 함정이다 — `unsigned long`은 ARM32에서 32비트, x86-64 리눅스에서 64비트라 호스트 테스트에서 통과하고 타깃에서 잘려 나간다. **폭을 원하면 `uint32_t`를 쓴다.** 한편 `mask_range`의 두 시프트 카운트는 각각 `31 - hi`와 `lo`라 항상 0..31이다. 그래서 `mask_range(31, 0)`이 `0xFFFFFFFF`가 되고 **시프트 32가 등장하지 않는다**.

| 만들고 싶은 것 | 안전한 식 | 흔한 함정 |
|---|---|---|
| bit n | `(uint32_t)1u << n` | `1 << n` (n=31에서 UB) |
| bit hi..lo | `(0xFFFFFFFFu >> (31-hi)) & (0xFFFFFFFFu << lo)` | `((1u<<(hi-lo+1))-1)<<lo` (폭 32에서 UB) |
| 폭 w | `mask_range(w-1, 0)` | `(1u << w) - 1` (w=32에서 UB) |
| 전부 1 | `0xFFFFFFFFu`, `~0u` | `1u << 32` (UB) |

폭 32 함정이 들어오는 경로는 뻔하다. 필드 폭을 `#define`으로 받아 쓰다가 어느 날 "레지스터 전체"를 뜻하는 32가 들어온다.

---

## 3. set / clear / toggle / test (문제 06)

```c
v | mask              /* set    : 멱등 */
v & ~mask             /* clear  : 멱등. mask 가 uint32_t 여야 옳다 */
v ^ mask              /* toggle : 두 번 적용하면 항등 */
(v & mask) == mask    /* ALL  */
(v & mask) != 0u      /* ANY  */
(v & mask) == 0u      /* NONE */
```

`~mask`의 함정. `mask`가 `uint8_t`나 `int`면 `~` 전에 `int`로 승격된다. `uint8_t m = 0x0F`에서 `~m`은 `0xFFFFFFF0`(int)이고, `v`가 `uint32_t`면 `v & ~m`이 상위 24비트까지 함께 지운다. **마스크 타입을 처음부터 `uint32_t`로 고정**하는 이유다.

`mask == 0`의 규약도 먼저 정한다. 이 문제 세트는 ALL = true(공집합은 모두 만족), ANY = false, NONE = true다. 반대로 정하면 "설정된 플래그가 없을 때 전부 준비됐다고 판단"하는 류의 버그가 호출부에서 생긴다. 분기 없는 조건부 set/clear는 `(v & ~mask) | ((uint32_t)-(uint32_t)on & mask)`다. `-(uint32_t)on`은 `on`이 1이면 `0xFFFFFFFF`, 0이면 `0`이다. unsigned의 단항 마이너스는 `2^32 - x`로 **정의되어 있다**(signed와 달리 UB가 아니다). 분기 예측이 없는 Cortex-M0에서 조건 분기를 없애려고 쓴다.

---

## 4. 비트 범위 추출과 삽입 (문제 06)

```c
/* extract: (v & mask_range(hi, lo)) >> lo */
static uint32_t bits_insert(uint32_t v, unsigned hi, unsigned lo, uint32_t field)
{
    uint32_t m = mask_range(hi, lo);
    return (v & ~m) | ((field << lo) & m);
}
```

```text
bits_insert(0xAAAA5555, 15, 8, 0x3C)

  v      = 1010 1010 1010 1010 0101 0101 0101 0101
  m      = 0000 0000 0000 0000 1111 1111 0000 0000
  v & ~m = 1010 1010 1010 1010 0000 0000 0101 0101   <- 필드 자리만 비운다
  f << 8 = 0000 0000 0000 0000 0011 1100 0000 0000
  OR     = 1010 1010 1010 1010 0011 1100 0101 0101 = 0xAAAA3C55
           ^^^^^^^^^^^^^^^^^^^               ^^^^^^^^^
           한 비트도 안 변했다               여기도 그대로
```

`& m`을 빼면 `field`의 넘친 비트가 **이웃 필드로 새어 나간다**. 4비트 자리에 `0x1FF`를 넣으면 `0x1FF00`이 되어 bit 12 이상을 오염시킨다. 레지스터에서 이 버그는 "분주비를 바꿨는데 인터럽트 enable이 켜졌다"로 나타난다.

`field << lo`는 왜 UB가 아닌가. `field`가 `uint32_t`이고 `lo <= 31`이므로 unsigned 시프트다. 넘친 비트는 조용히 버려진다(mod 2^32) — unsigned에서는 이것이 **정의된 동작**이다.

---

## 5. 비트 세기 — popcount 네 가지 (문제 07)

| 방식 | 사이클 성질 | 코드 크기 | 언제 쓰나 |
|---|---|---|---|
| naive (32회 루프) | 입력과 무관하게 일정 | 가장 작다 | 타이밍이 새면 안 될 때(암호), 코드 크기 우선 |
| Kernighan | 세워진 비트 수만큼 | 작다 | 희소 비트맵(인터럽트 pending) |
| 니블 테이블 16B | 항상 8회 조회, 분기 없음 | 테이블 16B | 파이프라인 친화 |
| SWAR | 고정, 루프·테이블 없음 | 상수 5개 | 곱셈이 빠른 코어, 최고 속도 |

```text
x &= x - 1  은 "가장 낮은 1 비트 하나를 지운다"

  x     = 0101 1000
  x - 1 = 0101 0111     최하위 1이 0이 되고 그 아래가 전부 1이 된다
  AND   = 0101 0000     최하위 1 하나만 사라졌다

  0x5C(0101 1100) -> 0x58 -> 0x50 -> 0x40 -> 0  =  4회 = popcount 4
```

이 관용구는 popcount 말고도 쓴다. **pending 비트맵 순회**의 정석이 `while (p) { int i = ffs(p); handle(i); p &= p - 1; }`이고, 인터럽트 컨트롤러 pending 레지스터를 처리하는 코드가 정확히 이 모양이다. 테이블 크기는 면접에서 자주 되묻는다. 니블 16바이트는 조회 8회, 바이트 256바이트는 조회 4회. 128KB flash MCU에서 256바이트는 0.2%라 쓸 수 있고, 32KB짜리에 라이브러리가 열 개 들어 있으면 이야기가 다르다. **숫자로 답하는 것이 중요하다.**

---

## 6. 뒤집기·찾기·2의 거듭제곱 (문제 07)

```text
bit reverse: 1 -> 2 -> 4 (-> 8 -> 16) 단계로 접는다. 8비트만 그려 보면

  입력      a b c d e f g h
  1비트 쌍  b a d c f e h g
  2비트 쌍  d c b a h g f e
  4비트 쌍  h g f e d c b a      <- 8비트 완료
```

32비트는 8비트 쌍, 16비트 쌍 교환을 더해 다섯 단계다. 32번 루프보다 빠르고 **상수 시간**이다. 쓰이는 곳은 LSB-first SPI/CRC와 MSB-first 데이터를 맞출 때, 비트 역순 FFT 인덱싱. 검증에 좋은 두 성질: 두 번 적용하면 항등, popcount를 보존한다.

```c
/* find_first_set: 0 기반, 없으면 -1. 절반씩 좁혀 항상 5번 비교 */
if (x == 0u) return -1;
int n = 0;
if ((x & 0x0000FFFFu) == 0u) { n += 16; x >>= 16; }   /* 이하 0xFF, 0xF, 0x3, 0x1 */
if ((x & 0x00000001u) == 0u) { n += 1; }
return n;
```

`find_last_set`은 조건을 `!= 0`으로 뒤집고 마스크를 위쪽(`0xFFFF0000`, `0xFF00`, ...)으로 바꿔 내려온다. `x == 0`의 규약을 꼭 정한다. POSIX `ffs()`는 1 기반이고 0을 "없음"으로 쓰는데, 두 규약을 한 코드베이스에 섞으면 off-by-one이 난다. `31 - find_last_set(x)`가 곧 **CLZ**이고 ARMv7-M 이상에는 `CLZ` 명령이 있다. Cortex-M0에는 없어서 손으로 짠 버전이 남는다.

`x & -x`는 가장 낮은 1 비트만 남긴다 — `-x`는 unsigned에서 `2^32 - x`로 정의되어 있어 안전하고, signed였다면 `-INT_MIN`이 UB다.

```text
is_power_of_two: x != 0 && (x & (x - 1)) == 0
  x != 0 을 빼면 0이 통과하고, ring_init(buf, 0) 이 통과해
  인덱스 마스크가 0 - 1 = 0xFFFFFFFF 가 되어 아무 데나 쓴다.

next_power_of_two(100)
  x-1 = 99  = 0110 0011
  |= >>1    = 0111 0011
  |= >>2    = 0111 1111     <- 최상위 1 아래를 전부 1로 채운다 (smear)
  |= >>4, >>8, >>16
  +1        = 1000 0000 = 128

  0 -> 1,  1 -> 1,  128 -> 128,  0x80000000 -> 0x80000000,  0x80000001 -> 0(불가)
```

`x--`로 시작하는 이유는 이미 2의 거듭제곱인 입력을 그대로 두기 위해서다. `0x80000001` 이상은 32비트에 담기지 않으므로 오류 값을 정해야 한다 — 0을 돌려주거나 `bool` + out-parameter로 나누는 쪽이 더 명시적이다.

---

## 7. 엔디언 (문제 08)

엔디언은 **메모리에 바이트를 놓는 순서**일 뿐이다. 레지스터 안에서는 순서가 없다.

```text
uint32_t v = 0x0A0B0C0D;  를 메모리에 놓으면

  little endian (x86, ARM 기본) : 낮은 주소에 낮은 바이트
    addr+0  addr+1  addr+2  addr+3
     0x0D    0x0C    0x0B    0x0A

  big endian (네트워크 바이트 순서, 일부 MIPS/PowerPC)
     0x0A    0x0B    0x0C    0x0D
```

런타임 판별은 `uint16_t probe = 0x0102u`를 `memcpy`로 바이트 배열에 꺼내 `b[0]`을 보는 것이다. `memcpy`는 정렬도 aliasing도 문제가 없고 `-O2`에서 상수로 접힌다. 실무에서는 컴파일 타임 매크로(`__BYTE_ORDER__`)가 낫다. **진짜 교훈은 이것이다.** 프로토콜 필드를 읽을 때 host 엔디언을 알 필요가 아예 없게 짜면 된다.

```c
const unsigned char *b = p;                 /* load_be32 */
return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
       ((uint32_t)b[2] <<  8) | ((uint32_t)b[3]);
```

host가 big이든 little이든 같은 값을 주고 정렬도 요구하지 않는다. `(uint32_t)` 캐스트를 빼면 `b[0] << 24`가 `int` 시프트가 되어 `b[0] >= 0x80`에서 `int` 범위를 넘는다 = UB다.

| 하고 싶은 것 | 쓸 것 | 쓰지 말 것 |
|---|---|---|
| 프로토콜 필드 읽기 | `load_be32` / `load_le32` (바이트 조립) | `*(uint32_t *)p`, `ntohl(*(uint32_t*)p)` |
| host 순서로 값 복사 | `memcpy` | 포인터 캐스트 |
| 순서만 뒤집기 | `bswap32` | `htonl` (호스트 헤더 의존) |
| 구조체를 그대로 전송 | 필드별 직렬화 | `write(fd, &s, sizeof s)` |

---

## 8. 정렬과 unaligned 접근 (문제 08)

```text
와이어 포맷. seq(u32) 가 offset 3 = 절대 4바이트 정렬이 아니다.
  offset 0    1        3           7         9
        +-----+--------+-----------+---------+--------------+
        | ver | len BE | seq BE    | crc BE  | payload ...  |
        +-----+--------+-----------+---------+--------------+
```

`(uint32_t *)(p + 3)`은 두 가지를 동시에 위반한다.

**위반 1: 정렬.** 표준은 "잘못 정렬된 포인터를 통한 접근"을 UB로 둔다. x86은 하드웨어가 두 번 읽어 붙여 주므로 조용히 동작한다. Cortex-M0/M3의 워드 접근과 `LDM`/`STM`, 많은 DSP는 **HardFault** 또는 조용한 오답을 낸다. 그래서 "호스트에서 테스트했습니다"가 안전을 보장하지 않는다.

**위반 2: strict aliasing.** 같은 메모리를 `unsigned char*`와 `uint32_t*`로 동시에 보면 컴파일러는 "이 둘은 겹치지 않는다"고 가정할 권리가 있다. 증상이 크래시가 아니라 **재배치**로 나온다. 바이트를 채워 넣은 코드가 워드 읽기 뒤로 밀려 이전 값이 읽히는 식이다. `-O0`에서 멀쩡하고 `-O2`에서만 깨지는 대표적 이유다.

정답은 `memcpy`를 쓰거나 바이트를 직접 조립하는 것이고, 둘 다 `-O2`에서 한두 명령으로 접힌다(ARM은 `LDR` + `REV`). "memcpy는 느리다"는 걱정은 상수 크기에서 근거가 없다.

```text
align_up(13, 8)

  13      = 0000 1101
  + (8-1) = 0001 0100  (= 20)
  & ~7    = 0001 0000  (= 16)     하위 3비트를 잘라 내림

  (v + a - 1) / a * a 는 나눗셈 두 번. Cortex-M0 은 나눗셈 명령이 없어
  __aeabi_uidiv 호출로 수십 사이클이 든다.
```

`a`는 2의 거듭제곱이어야 한다 — 그래야 `a - 1`이 하위 마스크가 된다. 그리고 `v + a - 1`이 `SIZE_MAX`를 넘는 경우도 막아야 한다. 넘치면 조용히 작은 값이 나와 버퍼 앞쪽을 가리킨다. 두 전제 모두 `assert`로 못박는다. 테스트에는 불변식 네 개를 박아 둔다. `align_down(v,a) <= v <= align_up(v,a)`, 둘 다 `a`의 배수, `align_up(v,a) - v < a`, 그리고 `v`가 이미 정렬되어 있으면 셋이 모두 같다.

포인터 쪽은 `uintptr_t`를 거친다. 한 가지 더: **버퍼 범위를 벗어난 포인터는 만들지도 않는다.** 계산만 해도 UB다. 그래서 할당자의 경계 검사는 포인터 비교가 아니라 오프셋 정수로 한다. `start = align_up(used, a); if (start > size || n > size - start) return NULL;`

---

## 9. 정수 오버플로와 포화 (문제 09)

**unsigned는 랩어라운드가 정의되어 있다.** `UINT32_MAX + 1 == 0`은 표준이 보장한다. 그래서 `uint32_t s = a + b; return (s < a) ? UINT32_MAX : s;`처럼 결과를 보고 판정해도 된다. 합이 작아졌다 = 자리올림이 밖으로 나갔다.

**signed는 오버플로가 UB다.** 그리고 이게 실전에서 무섭게 나타난다.

```text
    int32_t s = a + b;
    if (s < a) { /* 오버플로 처리 */ }        <- 컴파일러가 이 if 를 지운다

  컴파일러의 추론: "오버플로는 UB 이고 UB 는 일어나지 않는다고 가정해도 된다.
  오버플로가 없으면 b >= 0 일 때 s >= a 가 항상 참이다. 분기를 삭제한다."
  -O0 에서는 남고 -O2 에서 사라진다. 테스트는 통과하고 필드에서 터진다.
```

규칙은 하나다. **오버플로할 수 있는 signed 산술은 `uint32_t`에서 한다.**

```c
static bool s32_add_overflow(int32_t a, int32_t b, int32_t *out)
{
    uint32_t ua = (uint32_t)a, ub = (uint32_t)b, us = ua + ub;
    bool ovf = (((ua ^ us) & (ub ^ us) & 0x80000000u) != 0u);
    if (!ovf && out) *out = u32_to_s32(us);
    return ovf;
}
```

```text
(a ^ s) & (b ^ s) & 0x80000000

  a 와 s 의 부호가 다르고 AND b 와 s 의 부호도 다를 때만 최상위 비트가 1.
  a 와 b 의 부호가 서로 다르면 오버플로가 불가능하고 이 식은 자동으로 0이다.

  a=+MAX, b=+1   -> s=0x80000000 : a^s=0xFFFFFFFF, b^s=0x7FFFFFFF -> 오버플로
  a=+MAX, b=MIN  -> s=0xFFFFFFFF : a^s=0x80000000, b^s=0x7FFFFFFF -> 정상(-1)
```

뺄셈은 덧셈으로 돌리면 안 된다. `-b`가 `b == INT32_MIN`에서 표현 불가다(`-INT32_MIN`은 UB). 그래서 뺄셈 판정식을 따로 쓴다. 또 C11에서 **범위를 벗어난 unsigned → signed 변환은 구현 정의**다(UB는 아니지만 표준이 값을 정해 주지 않는다. C23에서 2의 보수로 확정됐다). 표준 안에서 쓰려면 `u <= INT32_MAX`면 그대로 캐스트하고, 아니면 `(int32_t)(u - 0x80000000u) + INT32_MIN`으로 두 단계를 모두 범위 안에서 처리한다. 같은 이유로 음수의 `>>`도 구현 정의다. DSP의 `ASR`과 같은 의미를 표준 안에서 재현하려면 unsigned로 밀고 위쪽을 1로 채운다.

```text
오디오에서 들리는 차이
  포화(saturating)          랩어라운드(wrapping)
      ____                      ____
     /    \                    /    |
  __/      \__           ____ /     |   ____   <- +full 이 -full 로 점프
  머리가 눌린다(distortion)   "딱" 하는 임펄스(click). 스피커에 훨씬 나쁘다.
```

`int16_t` 샘플 두 개를 더할 때는 `int32_t`로 올려 계산한 뒤 clamp하는 것이 가장 읽기 쉽다 — `|s| <= 65536`이라 오버플로가 원천적으로 불가능하다.

---

## 10. 고정소수점 Q15와 나눗셈 없는 산술 (문제 09)

Q15는 `int16_t`를 `2^15`로 나눈 값으로 읽는 규약이다. FPU가 없는 코어에서 소수를 다루는 표준 수단이다.

```text
  0x4000 = 16384 = 0.5   0x7FFF = 32767 = 0.99997   0x8000 = -32768 = -1.0
  범위 [-1.0, +0.99997].  +1.0 은 표현할 수 없다.

  a (Q15)   s.fff ffff ffff ffff                    16비트
  b (Q15)   s.fff ffff ffff ffff                    16비트
  a * b     ss.ff ffff ffff ffff ffff ffff ffff ff  32비트 = Q30
            ^^ 부호 비트 2개. 15비트 내리면 다시 Q15.
               버릴 하위 15비트의 절반(1 << 14)을 미리 더하면 반올림.
```

```c
static int16_t q15_mul(int16_t a, int16_t b)
{
    int32_t p = (int32_t)a * (int32_t)b;      /* |p| <= 2^30, int32 안에서 안전 */
    int32_t r = asr32(p + (1 << 14), 15);     /* 반올림 후 내림 */
    if (r > INT16_MAX) return INT16_MAX;      /* (-1.0) x (-1.0) 만 여기로 온다 */
    if (r < INT16_MIN) return INT16_MIN;
    return (int16_t)r;
}
```

포화가 필요한 입력이 **정확히 한 쌍**이다. `(-1.0) x (-1.0) = +1.0`인데 `+1.0`이 Q15에 없다. 이 한 쌍을 잡아내는지가 채점 포인트다. 반올림을 빼면 결과가 항상 `-무한대` 쪽으로 깎여 평균 `-0.5 LSB`의 **DC offset**이 생기고, 필터를 여러 단 통과하면 누적된다. 오디오에서는 무음 구간의 미세한 바이어스로 들어간다.

```text
나눗셈 없는 평균
  a + b = (a & b) * 2 + (a ^ b)
    (a & b) = 두 수가 공통으로 가진 비트 = 자리올림이 확실한 부분
    (a ^ b) = 한쪽에만 있는 비트
  => (a + b) / 2 = (a & b) + ((a ^ b) >> 1)

  a = b = 0xFFFFFFFF  ->  0xFFFFFFFF + 0 = 0xFFFFFFFF   (넘치지 않는다)
  a = -1, b = 1       ->  1 + asr(0xFFFFFFFE, 1) = 1 + (-1) = 0
                          논리 시프트였다면 1 + 0x7FFFFFFF = INT32_MIN  <- 버그
```

부호 있는 버전은 시프트가 **산술**이어야 한다. 이 한 줄이 이 관용구에서 가장 많이 틀리는 곳이다.

---

## 흔한 함정

| 함정 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `1 << 31` | UBSan 경고, 최적화에서 이상 동작 | `int` 왼쪽 시프트 결과가 타입 범위를 넘음 | `(uint32_t)1u << 31` |
| `1u << 32`, `x >> 32` | 0일 것 같은데 아무 값이나 나온다 | 시프트 카운트 >= 폭은 UB | 전체 마스크는 `0xFFFFFFFFu`를 깎아 만든다 |
| `~mask`에서 mask가 `uint8_t`/`int` | 상위 비트까지 지워진다 | 정수 승격으로 `int`가 되어 32비트에 `~` 적용 | 마스크 타입을 `uint32_t`로 고정 |
| `b[0] << 24` (b가 `unsigned char`) | `b[0] >= 0x80`에서 UB | `int` 시프트가 범위를 넘음 | `(uint32_t)b[0] << 24` |
| `*(uint32_t *)(p + 3)` | x86 OK, Cortex-M0 HardFault | 정렬 위반 + strict aliasing 위반 | `memcpy` 또는 바이트 조립 |
| `write(fd, &s, sizeof s)` | 상대편이 다른 값을 읽는다 | 패딩·엔디언·비트필드 배치가 ABI 의존 | 필드별 직렬화 |
| `a`가 2의 거듭제곱이 아닌 `align_up` | 조용히 엉뚱한 주소 | `a - 1`이 마스크가 아니다 | `assert(is_pow2_size(a))` |
| `if (a + b < a)` (signed) | `-O2`에서 검사가 사라진다 | signed 오버플로 UB 전제로 분기 삭제 | `uint32_t`에서 계산, 부호 비트로 판정 |
| `-b`로 뺄셈을 덧셈으로 | `b == INT32_MIN`에서 UB | `-INT32_MIN`은 표현 불가 | 뺄셈 판정식을 따로 쓴다 |
| `(int32_t)u` (범위 초과) | 값이 컴파일러 의존 | C11에서 구현 정의 | `u32_to_s32()`로 두 단계 변환 |
| 음수 `>> 1` | 컴파일러/타깃 의존 | 표준이 구현 정의로 남겨 둠 | 명시적 `asr32()` 헬퍼 |
| `is_power_of_two(0)`이 참 | `ring_init(buf, 0)`이 통과한다 | `x != 0` 검사 누락 | `x != 0 && (x & (x-1)) == 0` |
| Q15 곱에서 반올림 누락 | 무음에 미세한 바이어스 | 항상 `-무한대` 쪽으로 깎임 | `p + (1 << 14)` 후 시프트 |
| 랩어라운드 믹싱 | "딱" 하는 클릭 | `+full`이 `-full`로 점프 | 포화 연산으로 clamp |

---

## 면접 질문 10개

**1. Why is `1 << 31` undefined behaviour?**

`1`은 `int`다. signed 왼쪽 시프트는 결과가 그 타입에 표현 가능해야 하는데 `1 << 31`은 32비트 `int` 범위를 넘는다. 실무에서는 대부분 원하는 값이 나오지만 UBSan이 잡고 최적화 결정에 영향을 준다. 그래서 마스크는 항상 `(uint32_t)1u << n`으로 쓴다. — **English**: "`1` is an `int`, so shifting into the sign bit is signed overflow, which is undefined; I write `(uint32_t)1u << n` so the shift happens on an unsigned type of a known width."

**2. How do you build a 32-bit-wide mask?**

시프트로는 못 만든다. `1u << 32`는 카운트가 폭과 같아 UB다. `0xFFFFFFFFu`를 양쪽에서 깎는다. `(0xFFFFFFFFu >> (31 - hi)) & (0xFFFFFFFFu << lo)`면 두 시프트 카운트가 항상 0..31이라 `hi=31, lo=0`에서도 안전하다. — **English**: "You cannot get there with a shift, because a shift count equal to the width is undefined, so I mask down from `0xFFFFFFFF` on both sides and every shift count stays inside zero to thirty-one."

**3. What does `x &= x - 1` do, and where do you use it?**

가장 낮은 1 비트 하나를 지운다. `x - 1`이 최하위 1을 0으로 만들고 그 아래를 모두 1로 만들기 때문이다. popcount를 세워진 비트 수만큼만 돌게 하고, 인터럽트 pending 비트맵을 순회할 때 "처리한 비트만 지우는" 한 줄로 쓴다. — **English**: "It clears the lowest set bit, which lets me iterate a sparse pending bitmap in exactly as many steps as there are bits set."

**4. Zero is passed to `find_first_set`. What do you return?**

규약을 먼저 정하는 것이 답이다. 나는 0 기반 인덱스에 "없음"은 `-1`로 쓴다. POSIX `ffs()`는 1 기반이고 0을 "없음"으로 쓰는데, 두 규약을 한 코드베이스에 섞으면 off-by-one이 난다. 어느 쪽이냐보다 헤더에 명시하고 테스트로 박아 두는 것이 중요하다. — **English**: "I return minus one and document it, because the real bug here is mixing two conventions — POSIX `ffs` is one-based and uses zero for 'none'."

**5. What is wrong with `*(uint32_t *)(p + 3)`?**

두 가지를 동시에 위반한다. 정렬 — 잘못 정렬된 포인터 접근은 UB이고 Cortex-M0/M3에서 HardFault다. strict aliasing — 컴파일러가 `unsigned char`와 `uint32_t` 접근이 겹치지 않는다고 가정해 `-O2`에서 순서를 바꾼다. 해결은 `memcpy`나 바이트 조립이고 둘 다 한두 명령으로 접힌다. — **English**: "It breaks alignment and strict aliasing at the same time: the first faults on Cortex-M, and the second lets the compiler reorder my stores at `-O2`, so I use `memcpy` or assemble the bytes by hand."

**6. Why does `memcpy` not make this slow?**

크기가 컴파일 타임 상수면 gcc/clang은 호출로 남기지 않고 로드/스토어 한두 개로 인라인한다. unaligned 접근이 되는 코어에서는 그대로 한 명령, 안 되는 코어에서는 바이트 네 개를 붙인다. 정확성은 공짜로 얻고 비용은 타깃이 실제로 요구하는 만큼만 낸다. — **English**: "With a constant size the compiler inlines it into one or two loads, so I get defined behaviour for free and pay only what the target actually requires."

**7. Round a size up to a 32-byte boundary without dividing.**

`(v + 31) & ~31`이다. 일반화하면 `(v + (a-1)) & ~(a-1)`이고 `a`가 2의 거듭제곱이라는 전제가 필요하다 — 그래야 `a - 1`이 하위 마스크가 된다. 나눗셈을 피하는 이유는 Cortex-M0에 나눗셈 명령이 없어 라이브러리 호출이 되기 때문이다. `v + a - 1`이 넘치지 않는지도 확인해야 한다. — **English**: "It is `(v + (a - 1)) & ~(a - 1)` with an assert that `a` is a power of two, which avoids the division a Cortex-M0 would have to call a library routine for."

**8. Why is `if (a + b < a)` wrong for signed integers?**

`a + b`에서 이미 오버플로가 일어나면 그 시점에 UB다. 컴파일러는 UB가 없다고 가정할 권리가 있으므로 "오버플로가 없으면 이 조건은 절대 참이 아니다"라고 추론해 분기를 삭제한다. `-O0`에서는 남고 `-O2`에서 사라진다. 그래서 검사는 `uint32_t`에서 하고 부호 비트로 판정한다. — **English**: "The addition itself is already undefined, so at `-O2` the compiler proves the branch unreachable and deletes the check; I do the arithmetic in `uint32_t` and test the sign bits instead."

**9. Wrapping versus saturating for audio samples.**

랩어라운드는 최대 양수 샘플을 최대 음수로 점프시킨다. 파형에 수직 임펄스가 생겨 "딱" 소리로 들리고 스피커에 나쁘다. 포화는 파형 머리를 평평하게 눌러 distortion으로 들린다. 훨씬 낫다. 그래서 믹싱 경로에서는 `int32_t`로 올려 더하고 `int16_t` 범위로 clamp한다. — **English**: "Wrapping turns a loud positive sample into a full-scale negative one, which the user hears as a click, while saturating only flattens the peak, so mixers clamp instead of wrap."

**10. What is Q15, and which multiply needs saturation?**

`int16_t`를 `2^15`로 나눈 값으로 읽는 고정소수점 규약이고 범위는 `-1.0`부터 `+0.99997`이다. Q15 곱은 Q30이라 15비트 내려야 하고, 버릴 비트의 절반을 미리 더해 반올림한다. 포화가 필요한 입력은 `(-1.0) x (-1.0) = +1.0` 한 쌍뿐이다. `+1.0`이 Q15에 없기 때문이다. — **English**: "Q15 is a fraction in `[-1.0, 0.99997]`; the product is Q30 so I round and shift down by fifteen, and the only case that needs clamping is minus one times minus one, because plus one is not representable."

---

## 이 레벨 문제들

| N | 문제 | 난이도 | 목표 | 이 노트에서 | 답안 |
|---|---|---|---|---|---|
| **06** | [비트 set/clear/toggle/test 와 비트 범위](../problems/06_bit_ops.html) | 기초 | 20분 | §2, §3, §4 | `solutions/06_bit_ops.c` |
| **07** | [popcount, 반전, ffs/fls, 2의 거듭제곱](../problems/07_bit_count_reverse.html) | 기초 | 25분 | §5, §6 | `solutions/07_bit_count_reverse.c` |
| **08** | [엔디언과 정렬, unaligned 접근](../problems/08_endian_align.html) | 중급 | 30분 | §7, §8 | `solutions/08_endian_align.c` |
| **09** | [포화 연산과 Q15 고정소수점](../problems/09_saturating_math.html) | 중급 | 30분 | §9, §10 | `solutions/09_saturating_math.c` |

06과 07은 워밍업이라 하루에 둘 다 끝낸다. 08과 09는 각각 하루를 준다. 다음은 [L1 메모리·포인터](L1_memory_pointers.html)다. 채점은 `coding/`에서 `make run N=06`(내 구현) / `make sol N=06`(모범답안).

---

## 체크리스트

- [ ] `1 << 31`이 UB인 이유와 폭 32 마스크를 시프트 없이 만드는 법을 말할 수 있다
- [ ] `~mask`에서 정수 승격이 어떻게 사고를 내는지 예를 들 수 있다
- [ ] 필드 삽입에서 `& mask`를 빼면 무엇이 오염되는지 말할 수 있다
- [ ] `x & (x-1)`과 `x & -x`를 보고 각각 무슨 뜻인지 바로 안다
- [ ] popcount 네 구현의 trade-off를 사이클과 바이트 수로 말할 수 있다
- [ ] `find_first_set(0)`과 `is_power_of_two(0)`의 규약을 실제 버그로 설명할 수 있다
- [ ] 바이트 스트림에서 BE 32비트 필드를 정렬 가정 없이 꺼낼 수 있다
- [ ] strict aliasing 위반의 증상이 크래시가 아니라 재배치인 이유를 안다
- [ ] `align_up`을 나눗셈 없이 쓰고 두 전제(2의 거듭제곱, 오버플로)를 검사한다
- [ ] signed 오버플로 검사를 `uint32_t`에서 하는 이유를 컴파일러 추론으로 설명할 수 있다
- [ ] `-INT32_MIN`이 왜 문제인지, 그래서 뺄셈을 따로 쓰는 이유를 안다
- [ ] Q15의 범위와 포화가 필요한 유일한 입력 쌍을 안다
- [ ] 나눗셈 없는 평균 식을 유도할 수 있고, 부호 있는 버전에서 산술 시프트가 필요한 이유를 안다
