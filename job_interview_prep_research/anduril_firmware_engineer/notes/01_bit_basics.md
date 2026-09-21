# 01. 비트 조작 기초 (Bit Manipulation Basics) 🔧

> MCU 레지스터 조작(register manipulation)의 기본 어휘. 드론 펌웨어에서 GPIO,
> 타이머, ADC, 통신 페리페럴(SPI/I2C/UART) 레지스터의 특정 비트를 켜고·끄고·읽는
> 일은 매 제어 루프마다 수백 번 일어난다. 이 10개는 그 "손가락 근육"이다.

---

## 왜 Anduril 펌웨어 인터뷰에 나오나

- 디바이스 드라이버는 결국 **메모리 맵드 레지스터(memory-mapped register)** 에 마스크를
  씌워 비트를 조작하는 일이다. `GPIOA->BSRR |= (1 << pin)` 같은 코드가 곧 `set_bit`.
- 화이트보드에서 "레지스터의 3번 비트만 켜라", "부호 없이 절대값 구해라(분기 없이)",
  "패리티 비트 계산해라" 같은 문제로 **UB 감각과 2의 보수 이해**를 본다.
- 실기체 예: 비행 컨트롤러의 IMU 인터럽트 상태 레지스터 폴링, 모터 ESC의 폴트 플래그
  비트 판독, 텔레메트리 패킷의 패리티 검사.

---

## 핵심 치트시트 (cheatsheet)

| 연산 | 관용구 (idiom) | 메모 |
|------|----------------|------|
| Set bit n | `v \|=  (1u << n)` | OR |
| Clear bit n | `v &= ~(1u << n)` | 반전 마스크 AND |
| Toggle bit n | `v ^=  (1u << n)` | XOR |
| Test bit n | `(v >> n) & 1u` 또는 `v & (1u<<n)` | !=0 으로 bool 변환 |
| 최하위 1비트 지우기 | `v & (v - 1)` | popcount 루프에 사용 |
| 최하위 1비트만 남기기 | `v & (-v)` = `v & (~v + 1)` | isolate LSB |
| 부호 다름? | `(a ^ b) < 0` | 부호비트(MSB) 비교 |
| 분기 없는 abs | `(v ^ (v>>31)) - (v>>31)` | 아래 UB 주의 |

---

## 문제별 요점

### 1–4. Set / Clear / Toggle / Test
```c
value |  (1u << bit);   // set
value & ~(1u << bit);   // clear
value ^  (1u << bit);   // toggle
value &  (1u << bit);   // test (0 or non-zero)
```
**가장 흔한 함정:** `1 << bit` 는 `int`(부호 있음) 리터럴이라 `bit == 31`이면
부호비트를 건드려 **UB**. 항상 `1u`(또는 `1UL`)로 쓴다. 또 `bit >= 32`면
시프트 폭이 타입 폭 이상이라 그 자체가 UB — 드라이버 코드에선 방어적으로 guard.

### 5. swap_nibbles
```c
(uint8_t)((value << 4) | (value >> 4));
```
`uint8_t`는 연산 시 `int`로 승격되므로 `value << 4`의 상위 비트가 살아있다.
반드시 `(uint8_t)` 캐스트로 잘라준다. (BCD 코덱, LED 세그먼트 매핑에 흔함.)

### 6. parity_even — 짝수 패리티
1의 개수가 **짝수면 true**(0개도 짝수). 순진한 루프도 되지만 **XOR-folding**이
브랜치리스 O(log n):
```c
value ^= value >> 16;
value ^= value >> 8;
value ^= value >> 4;
value ^= value >> 2;
value ^= value >> 1;
return (value & 1u) == 0u;   // 최하위=1 이면 1의 개수 홀수
```
> 원본 소스의 주석/기대값이 서로 어긋나 있었다(어떤 곳은 0, 어떤 곳은 1).
> 여기서는 **"짝수 개 → true"** 로 정의를 고정했다. `parity_even(0x5)` = 1이 2개 = **true**.

### 7. clear_lsb — `v & (v-1)`
1을 빼면 최하위 1은 0이 되고 그 아래는 전부 1로 바뀐다. AND 하면 그 자리만 지워진다.
`v == 0`이면 `0u - 1 = 0xFFFFFFFF`, `0 & ... = 0` 이라 안전. Brian Kernighan
popcount의 핵심 스텝.

### 8. rightmost_set_bit — `v & (-v)`
2의 보수에서 `-v == ~v + 1`. AND 하면 최하위 1비트만 남는다. **unsigned에서
`0u - v`로 쓰면** 부정(negation) wrap이 well-defined. 인터럽트 우선순위에서
"가장 낮은 대기 IRQ" 뽑기 같은 데 쓴다.

### 9. is_opposite_sign — `(a ^ b) < 0`
부호가 다르면 XOR의 MSB(부호비트)가 1 → 음수. 0은 부호비트 0이라 양수 취급.
분기·곱셈 없이 한 줄.

### 10. abs_no_branch
```c
uint32_t uv = (uint32_t)value;
uint32_t mask = 0u - (uv >> 31);   // 음수면 0xFFFFFFFF, 양수면 0
return (int32_t)((uv ^ mask) - mask);
```
`value >> 31`(signed)은 음수 우측 시프트라 **구현 정의(implementation-defined)**.
그래서 unsigned로 부호비트를 뽑아 `0u - bit`로 마스크를 만들고, 전 과정을
unsigned로 계산해 **signed overflow UB를 회피**한다.
**주의:** `INT32_MIN`의 절대값은 `int32_t`로 표현 불가 → 그대로 `INT32_MIN` 반환(수학적 오류지만 UB는 아님).

---

## 인터뷰 팔로업 (follow-ups)

- **"왜 `1` 대신 `1u`?"** → 31번 비트 시프트 시 부호비트 UB 회피.
- **"`bit`가 32 이상이면?"** → 시프트 폭 ≥ 타입 폭은 UB. guard 하거나 문서화.
- **"`x & (x-1) == 0`은 무엇을 판별?"** → 2의 거듭제곱(또는 0) 여부.
- **"popcount는?"** → `while(v){ v &= v-1; cnt++; }` (set 비트 수만큼 반복) 또는 하드웨어 `__builtin_popcount`.
- **"volatile 레지스터에 read-modify-write 할 때 주의점?"** → 컴파일러 재정렬/최적화 방지 위해 `volatile`, ISR과 공유 시 원자성(레지스터 세트/클리어 전용 레지스터 BSRR 사용) 고려.

## 흔한 버그 (gotchas)

- `1 << 31` 부호 UB, `1 << 32` 폭 초과 UB.
- `uint8_t` 니블 스왑에서 캐스트 누락 → 상위 비트 잔존.
- signed `>>`를 논리 시프트로 착각(구현 정의).
- `abs(INT32_MIN)` 오버플로.
- 패리티 정의(짝수=true vs 홀수=true) 혼동 — 반드시 명세를 물어라.

---

## [추가] 11~17 · 범위 마스크 (lo~hi 비트 세우기/끄기/읽기/쓰기) ★ 빈출

> "3번 비트부터 7번 비트까지 세워라/꺼라/그 필드 값을 읽어라/새 값을 써라" — 온사이트에서 가장 자주 나오는 비트 문제.
> 범위는 **[lo, hi] 양 끝 포함**, 비트 번호는 0부터. 면접에서는 **inclusive인지, 위치+개수 표기인지 먼저 확인**할 것.

### 핵심은 마스크 하나
```c
/* [lo, hi] 만 1인 마스크 — 시프트 폭이 항상 0..31 이라 UB 없음 */
uint32_t mask = (~0u >> (31 - hi)) & (~0u << lo);
```
| 연산 | 식 | 예: lo=4, hi=7, mask=0xF0 |
|---|---|---|
| 세우기 | `value \| mask` | `0x00 → 0xF0` |
| 끄기 | `value & ~mask` | `0xFF → 0x0F` |
| 뒤집기 | `value ^ mask` | `0xF0 → 0x00` |
| 읽기 | `(value & mask) >> lo` | `0xAB → 0xA` |
| 쓰기 | `(value & ~mask) \| ((field << lo) & mask)` | `0xAB, field=0x5 → 0x5B` |

### 가장 흔한 함정: 폭이 32일 때
```c
uint32_t width = hi - lo + 1;
uint32_t mask  = ((1u << width) - 1u) << lo;   /* width==32 이면 1u << 32 → UB! */
```
- 해결 1: `width == 32` 분기
- 해결 2: 위의 양쪽 자르기 식 `(~0u >> (31-hi)) & (~0u << lo)`
- **면접에서 이 함정을 먼저 말하면** 비트 조작을 제대로 아는 사람으로 보인다.

### 16. set_field — 실제 드라이버 코드
```c
reg = (reg & ~mask) | ((field << lo) & mask);
```
- `& mask`로 **field를 자르지 않으면** 폭보다 큰 값이 옆 필드를 덮어쓴다 → 면접관이 일부러 `0x1F`를 4비트 필드에 넣어본다.
- 실제 레지스터라면 `volatile`로 **읽기 1회 → 계산 → 쓰기 1회**. ISR과 같은 레지스터를 만지면 read-modify-write 레이스 → 인터럽트 잠깐 막기 또는 set/clear 전용 레지스터(BSRR) 사용.

### 17. 위치+개수 표기
`set_bits_n(value, pos, count)` = `[pos, pos+count-1]` 범위로 바꿔서 11번 재사용. 범위 검사는 `count > 32 - pos`처럼 **뺄셈으로** 해서 덧셈 오버플로를 피한다.

### 팔로업 질문
- **"매크로로 만들어 보라"** → `#define BITS_MASK(lo, hi) ((~0u >> (31u - (hi))) & (~0u << (lo)))` — 인자 괄호 필수, 인자가 두 번 평가되지 않는지 확인.
- **"비트필드 구조체(`struct { unsigned a:4; }`)를 쓰면 안 되나?"** → 비트 순서·패딩이 **구현 정의**라 레지스터·와이어 포맷에는 부적합. 명시적 마스크가 이식성 있음.
- **"64비트면?"** → `~0ull`, `63 - hi`로 바꾸기. 상수 접미사 `u`/`ull` 누락이 버그의 원천.
