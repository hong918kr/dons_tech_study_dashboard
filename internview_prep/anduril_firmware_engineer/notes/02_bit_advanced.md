# 비트 조작 심화 (Bit Manipulation Advanced) — Q11-25 🧮

> 레지스터 필드, 프로토콜 바이트 순서, 엔코더 gray code, popcount 등 드론/무기체계 펌웨어의 저수준 비트 연산 총정리. Anduril FW 인터뷰 단골 주제.

---

## 0. 왜 이게 인터뷰에 나오나

임베디드/펌웨어는 결국 **레지스터에 비트를 쓰고, 버스에서 바이트를 읽는** 일이다. Anduril 같은 방산 드론 회사의 FW 인터뷰에서는:

- **하드웨어 레지스터 필드** read/modify/write (`set_field`/`get_field`)
- **프로토콜 파서** — MAVLink/커스텀 텔레메트리의 network byte order 처리 (`swap_endian32`)
- **엔코더/ADC** — 기계식 회전 엔코더의 gray code 디코딩
- **상태 비트마스크** — 활성 채널 수 세기 (`hamming_weight`)
- **버퍼/DMA 정렬** — 2의 거듭제곱 크기 정렬 (`next_power_of_two`)

이 15문제는 그 근육을 만드는 드릴이다.

---

## 1. 반드시 기억할 UB(정의되지 않은 동작) 함정

C에서 **시프트량이 타입 폭 이상이면 UB**다. 32비트에서:

```c
uint32_t x;
x >> 32;   // UB! (0 이 아님)
x << 32;   // UB!
1u << 32;  // UB!
```

이 때문에 원본 코드의 두 버그가 나온다:

| 함수 | 원본 버그 | 수정 |
|------|-----------|------|
| `rotate_left/right` | `k==0` 이면 `n >> (32-0)` = `n >> 32` UB. `k>=32` 도 UB. | `k &= 31; if(k==0) return n;` |
| `next_power_of_two` | 스텁 미구현 (`if(n==0){} return 0;`) | 채움-후-+1 알고리즘 |
| `prev_power_of_two` | `p<<=1` 루프가 `n>=2^31` 에서 오버플로 → 무한루프 | 최상위 비트만 남기는 비트 채움 |
| `width==32` 필드 | `(1u<<32)-1` UB | `width>=32 ? 0xFFFFFFFF : ...` 분기 |

---

## 2. 문제별 핵심

### 11. reverseBits — 비트 순서 뒤집기
```c
uint32_t reverseBits(uint32_t n) {
    n = ((n >> 1) & 0x55555555u) | ((n & 0x55555555u) << 1);
    n = ((n >> 2) & 0x33333333u) | ((n & 0x33333333u) << 2);
    n = ((n >> 4) & 0x0F0F0F0Fu) | ((n & 0x0F0F0F0Fu) << 4);
    n = ((n >> 8) & 0x00FF00FFu) | ((n & 0x00FF00FFu) << 8);
    n = (n >> 16) | (n << 16);
    return n;
}
```
- **반복문 방식**은 코드가 단순(32회 루프), **SWAR**는 O(1) 상수 시간이라 임베디드에서 선호.
- lookup table(256B)은 빠르지만 작은 MCU에선 ROM 부담.

### 12. hamming_weight (popcount)
```c
while (n) { n &= (n - 1); count++; }   // Kernighan: 1의 개수만큼만 반복
```
- GCC/Clang은 `__builtin_popcount` 제공(하드웨어 popcnt 명령). 인터뷰에선 손으로.

### 13. is_power_of_two
```c
return n && !(n & (n - 1));   // 0 을 반드시 걸러라!
```
- 함정: `n==0` 이면 `!(0 & -1)` = true 가 되어버림 → `n &&` 필수.

### 14. swap_odd_even_bits
```c
return ((n & 0xAAAAAAAAu) >> 1) | ((n & 0x55555555u) << 1);
```

### 15-17. extract / set / invert bit range (`[start, end]` 포함)
```c
uint8_t width = end - start + 1;
uint32_t mask = (width >= 32) ? 0xFFFFFFFFu : (((1u << width) - 1u) << start);
// extract: (n & mask) >> start
// set:     (n & ~mask) | ((value << start) & mask)
// invert:  n ^ mask
```
- **핵심 마스크 관용구**: `(1u << width) - 1` = width개의 1. `width==32` 만 분기.

### 18. next / prev power of two
```c
uint32_t next_power_of_two(uint32_t n) {   // 올림
    if (n == 0) return 1;
    n--; n|=n>>1; n|=n>>2; n|=n>>4; n|=n>>8; n|=n>>16;
    return n + 1;
}
uint32_t prev_power_of_two(uint32_t n) {   // 내림 = 최상위 set bit
    if (n == 0) return 0;
    n|=n>>1; n|=n>>2; n|=n>>4; n|=n>>8; n|=n>>16;
    return n - (n >> 1);
}
```
- `n--` 을 먼저 하는 이유: 이미 2의 거듭제곱이면 자기 자신을 반환하기 위해.
- `next`: `n > 2^31` 이면 결과가 32비트 초과 → 0 오버플로 (호출측 책임).

### 19. xor_swap
```c
if (a == NULL || b == NULL || a == b) return;   // a==b 가드 필수 (0 됨)
*a ^= *b; *b ^= *a; *a ^= *b;
```
- 실무에선 임시변수가 더 빠르고 안전. **인터뷰 트릭**으로만 의미.

### 20. rotate_left / rotate_right
```c
uint32_t rotate_left(uint32_t n, uint8_t k) {
    k &= 31u; if (k == 0) return n;
    return (n << k) | (n >> (32 - k));
}
```
- **원본 버그**: `k==0`/`k>=32` UB. `k &= 31` + `k==0` early return 으로 해결.

### 21. swap_endian32 (프로토콜 byte order!)
```c
return ((n>>24)&0xFF) | ((n>>8)&0xFF00) | ((n<<8)&0xFF0000) | ((n<<24)&0xFF000000);
```
- network(big-endian) ↔ host(대개 little-endian ARM). `__builtin_bswap32` 도 있음.

### 22. get_field / set_field (레지스터 비트필드)
```c
uint32_t get_field(uint32_t reg, uint8_t pos, uint8_t width) {
    uint32_t fm = (width>=32) ? 0xFFFFFFFFu : ((1u<<width)-1u);
    return (reg >> pos) & fm;
}
uint32_t set_field(uint32_t reg, uint8_t pos, uint8_t width, uint32_t value) {
    uint32_t fm = (width>=32) ? 0xFFFFFFFFu : ((1u<<width)-1u);
    uint32_t mask = fm << pos;
    return (reg & ~mask) | ((value << pos) & mask);
}
```
- C의 `struct` bit-field 대신 직접 마스킹 → 이식성/명확성. MMIO는 `volatile` 필수(실무).

### 23-24. gray code (엔코더 오류 방지)
```c
uint32_t binary_to_gray(uint32_t n) { return n ^ (n >> 1); }
uint32_t gray_to_binary(uint32_t n) {
    uint32_t res = n;
    while (n >>= 1) res ^= n;
    return res;
}
```
- 연속 값이 **정확히 1비트만** 바뀜 → 기계식 엔코더 전이 오류 최소화.

### 25. mul_pow2 / div_pow2
```c
uint32_t mul_pow2(uint32_t n, uint8_t k) { return (k>=32) ? 0u : (n << k); }
uint32_t div_pow2(uint32_t n, uint8_t k) { return (k>=32) ? 0u : (n >> k); }
```
- **부호없는** 값에만 안전. 음수 나눗셈은 산술 시프트라 `-1 >> 1 == -1`(0 아님) → 별개 함정.

---

## 3. 인터뷰 팔로우업 (자주 나오는 꼬리질문)

1. **"popcount를 O(1)로 하려면?"** → SWAR 병렬 합산 또는 하드웨어 `popcnt`/`__builtin_popcount`.
2. **"rotate가 왜 UB가 될 수 있나?"** → `n >> (32-k)` 에서 `k==0` → shift by 32. `k &= 31` 로 정규화하되 `k==0` 별도 처리.
3. **"endianness를 런타임에 어떻게 감지?"** → `union { uint32_t u; uint8_t b[4]; }` 로 `b[0]` 확인.
4. **"MMIO 레지스터 read/modify/write의 위험?"** → 비원자적. ISR/DMA와 race → `volatile` + 인터럽트 마스킹 or 하드웨어 bit-band(Cortex-M).
5. **"gray code를 왜 쓰나?"** → 다중 비트 동시 전이 시 중간 글리치 값 방지 (엔코더, 클럭 도메인 크로싱 FIFO 포인터).
6. **"`n & (n-1)` 의 의미는?"** → 최하위 set bit 하나 제거. popcount, is_power_of_two 둘 다 여기서 파생.

---

## 4. 치트시트

| 관용구 | 의미 |
|--------|------|
| `n & (n-1)` | 최하위 1비트 제거 |
| `n & -n` | 최하위 1비트만 남김 (isolate) |
| `n \| (n-1)` | 최하위 0비트들 채움 |
| `n && !(n & (n-1))` | 2의 거듭제곱 판정 |
| `(1u << w) - 1` | w개의 1 마스크 (w<32 만!) |
| `x ^ (x>>1)` | binary → gray |
| `k &= 31` | 회전량 mod 32 정규화 |

빌드/실행:
```sh
cc -std=c11 -Wall -Wextra 02_bit_advanced.c -o /tmp/andb_bit_advanced && /tmp/andb_bit_advanced
```
