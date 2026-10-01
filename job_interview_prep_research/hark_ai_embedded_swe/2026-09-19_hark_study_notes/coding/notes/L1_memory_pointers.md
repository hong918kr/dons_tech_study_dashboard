# L1. 메모리와 포인터 — 주소, 정렬, volatile, 함수 포인터

> **목표**: "이 바이트가 어디 있고, 누가 언제 바꾸고, 어떻게 읽어야 안전한가"를 주소 단위로 말할 수 있게 된다. 문법이 아니라 메모리 모델이 주제다.
> **수록 문제**: [10 memcpy_move](../problems/10_memcpy_move.html) · [11 struct_layout](../problems/11_struct_layout.html) · [12 volatile_const](../problems/12_volatile_const.html) · [13 func_table](../problems/13_func_table.html)
> **언제 다시 보나**: 정렬 때문에 HardFault가 났을 때 · 통신 프레임 파서를 새로 쓸 때 · `volatile`을 붙일지 망설일 때 · 드라이버에 콜백 API를 설계할 때 · 면접 전날 밤

---

## 0. 왜 이게 면접에 나오나

BSP·펌웨어 면접의 메모리·포인터 질문은 지식 확인이 아니라 **디버깅 능력의 대리 지표**다. 필드 버그 보고서의 절대다수가 네 가지로 수렴한다.

- 정렬이 어긋난 접근 → 어떤 코어에서는 HardFault, 어떤 코어에서는 조용히 느려짐
- 구조체를 통신 프레임에 덮어씌워 읽음 → 컴파일러나 타깃을 바꾸는 순간 값이 틀어짐
- `volatile`을 빼먹거나, 반대로 원자성으로 착각 → 재현율 5%짜리 행(hang)
- 수명이 끝난 메모리를 가리키는 포인터 → 스택이 재사용될 때까지 멀쩡하다가 터짐

면접관이 듣고 싶은 것은 요약이 아니라 **어떤 코드가 어떤 명령으로 번역되고 그게 왜 하드웨어와 어긋나는지**다.

## 1. 주소 공간 지도 — 무엇을 가리킬 수 있는가

```
  0xE0000000  System (NVIC, SCB)                       ← volatile 필수
  0x40000000  Peripheral (UART, I2S, DMA, GPIO 레지스터) ← volatile 필수
                                                   CPU 없이 값이 변한다
  0x20000000  SRAM  .data/.bss 전역·static │ heap(보통 0B) ↓ ↑ stack
                    stack 지역 변수, 인자, 리턴 주소
  0x00000000  Flash vector table │ .text 코드
                    .rodata  const 전역, 문자열, 디스패치 테이블
```

- **`.rodata`에 쓰면** MPU가 있으면 즉사, 없으면 조용히 무시된다. `const`를 붙이는 이유는 컴파일러가 미리 잡아 주기 때문이다.
- **stack의 주소를 함수 밖으로 내보내면** 리턴한 순간 무효다. 다음 호출이 그 자리를 덮을 때까지 "잘 동작하는 것처럼" 보이는 것이 가장 잡기 어려운 부류다.
- **`0x40000000` 이상은** CPU가 가만히 있어도 변한다. 그래서 `volatile`이다. 반대로 SRAM의 평범한 변수에 `volatile`을 붙이는 것은 대개 설계 실수를 감추는 행동이다.
## 2. 정렬과 패딩 — 손으로 계산해 보기

규칙은 세 줄이다.

- 타입 `T`의 객체 주소는 `alignof(T)`의 배수여야 한다.
- 구조체 멤버는 **선언 순서대로**, 자기 정렬의 배수인 **가장 가까운 오프셋**에 놓인다.
- 구조체 정렬은 멤버 정렬의 최댓값이고, `sizeof`는 그 값의 배수로 올림된다.

마지막 줄이 필요한 이유는 **배열을 만들 수 있어야 하기 때문**이다. `sizeof`가 정렬의 배수가 아니면 `arr[1]`부터 정렬이 깨진다.

### 워크드 예제 — 문제 11의 구조체

```c
typedef struct {
    uint8_t  id;             /* align 1, size 1 */
    uint16_t seq;            /* align 2, size 2 */
    uint32_t timestamp_us;   /* align 4, size 4 */
    int16_t  temp_c_q8;      /* align 2, size 2 */
    uint8_t  flags;          /* align 1, size 1 */
} sensor_sample_t;
```

```
  cur=0   id    align 1 → 0은 배수        → id @0,    cur=1
  cur=1   seq   align 2 → 배수 아님 → 2   → seq @2,   cur=4   (1B 버림)
  cur=4   ts    align 4 → 4는 배수        → ts @4,    cur=8
  cur=8   temp  align 2 → 8은 배수        → temp @8,  cur=10
  cur=10  flags align 1 → 10은 배수       → flags @10, cur=11
  정렬 = max(1,2,4,2,1) = 4 → sizeof = 11을 4의 배수로 올림 = 12
```

```
  off   0    1    2    3    4    5    6    7    8    9   10   11
      ┌────┬────┬─────────┬───────────────────┬─────────┬────┬────┐
      │ id │ ▓▓ │   seq   │   timestamp_us    │  temp   │flg │ ▓▓ │
      └────┴────┴─────────┴───────────────────┴─────────┴────┴────┘
             ↑ 내부 padding 1B                              ↑ 후미 1B
               (seq를 2의 배수로)                    (sizeof를 4의 배수로)

  멤버 합 10 · sizeof 12 · padding 2 (16.7% 낭비)
```

### 순서만 바꿔서 4바이트 줄이기

같은 다섯 멤버를 최악의 순서로 늘어놓으면(`u8 id, u32 ts, u8 flags, u16 seq, i16 temp`) `sizeof`가 16, padding이 6이 된다. 정렬 큰 것부터 놓으면(`u32 ts, u16 seq, i16 temp, u8 id, u8 flags`) 12로 돌아온다. 같은 데이터에 4바이트 차이다.

**실무 규칙**: 정렬이 큰 멤버부터 선언한다. 항상 최적은 아니지만 충분히 좋고, 구조체가 수천 개 배열로 들어가는 자리에서는 이 차이가 KB 단위 RAM이다.

### 코드가 의존하는 것만 컴파일 타임에 못박기

`offsetof`로 레이아웃을 **물어보고**, `_Static_assert`로 **가정을 고정**한다. 오프셋 숫자를 박지 않는 것이 핵심이다 — 그 값은 ABI가 정하고, 다른 타깃에서 달라져도 코드는 옳을 수 있다.

```c
_Static_assert(offsetof(sensor_sample_t, seq) == 2, "...");   /* 나쁨: ABI가
                                     바뀌면 이유 없이 빌드가 깨진다 */

_Static_assert(offsetof(sensor_sample_t, id) == 0, "첫 멤버는 항상 0");
_Static_assert(offsetof(sensor_sample_t, seq) % alignof(uint16_t) == 0,
               "포인터를 꺼내 쓸 수 있으려면 자연 정렬이어야 한다");
_Static_assert(sizeof(sensor_sample_t) % alignof(sensor_sample_t) == 0,
               "배열로 만들 수 있어야 한다");
_Static_assert(CHAR_BIT == 8, "이 와이어 포맷은 8비트 바이트를 가정한다");
```

표준이 **보장하는 것**은 넷뿐이다. 첫 멤버의 오프셋이 0, 멤버 오프셋이 선언 순서대로 증가, 멤버끼리 겹치지 않음, `sizeof`가 정렬의 배수. 나머지 오프셋과 padding의 크기·위치·**값**은 전부 ABI 소관이고, padding 바이트의 값은 불특정이다.

## 3. 포인터 산술 — 단위는 바이트가 아니다

```c
uint32_t *p;  p + 1     /* 4바이트 전진 */
uint8_t  *q;  q + 1     /* 1바이트 전진 */
void     *v;  v + 1     /* ← 표준 C가 아니다. GNU 확장 */
```

`void *` 산술은 **표준에 없다.** GCC/Clang이 `sizeof(void)`를 1로 취급해 받아주지만 `-pedantic`에서 경고, 일부 상용 컴파일러에서는 에러다. 바이트로 걷고 싶으면 `unsigned char *`로 바꿔 걷는다. 정의 범위도 좁다. 배열 `a[N]`에서 유효한 포인터 값은 `a` ~ `a + N`이다. `a + N`(one-past-end)은 만들어도 되지만 **역참조는 안 된다.** `a - 1`이나 `a + N + 1`은 계산 자체가 undefined behaviour이고, 컴파일러가 그 규칙을 근거로 루프 경계 검사를 지워 버린 사례가 실제로 있다. 비교도 좁다. `<`, `>`는 **같은 객체 안**의 포인터끼리만 정의되고 서로 다른 객체의 주소 비교는 unspecified다. 평평한 주소 공간에서는 동작하지만, 의도를 분명히 하려면 `uintptr_t`로 바꾼다.

```c
/* 서로 다른 객체일 수 있는 두 구간의 겹침 판정 (문제 10) */
uintptr_t pa = (uintptr_t)a, pb = (uintptr_t)b;
return (pa < pb) ? ((pb - pa) < n) : ((pa - pb) < n);
/*        ↑ 차이만 본다. pa + n 을 계산하면 원리상 오버플로할 수 있다 */
```

## 4. 겹침과 복사 방향 — memcpy와 memmove의 실제 차이

"memmove는 겹침을 처리한다"는 구현을 설명하지 못한다. 실제 차이는 **복사 방향 하나**다.

```
  dst < src (왼쪽 당기기) → 앞(index 0)부터 안전
      src:      [ A  B  C  D  E  F ]    쓰는 자리는 이미 읽고 지나온 칸
      dst:   [ ...............      ]
  dst > src (오른쪽 밀기) → 뒤(index n-1)부터 안전
      src:   [ A  B  C  D  E  F ]       앞에서부터 쓰면 안 읽은 원본을 덮는다
      dst:         [ ................ ]
```

`memmove(b+4, b+0, 8)`을 앞방향 복사로 처리하면 이렇게 깨진다.

```
  초기          : 10 11 12 13 14 15 16 17 18 19 1A 1B
  i=0 d[4]=s[0] : 10 11 12 13 10 15 16 17 ...
  i=4 d[8]=s[4] : 10 11 12 13 10 11 12 13 10 ...  ← s[4]가 i=0에서 이미 덮였다
  결과          : 10 11 12 13 10 11 12 13 10 11 12 13  ← 4칸 주기로 되풀이
```

주기가 4인 이유는 `dst - src == 4`이기 때문이다. 이 패턴을 보면 원인을 바로 지목할 수 있다. 그리고 `memmove`는 **겹침을 판정하지 않는다** — 겹치지 않으면 어느 방향이든 결과가 같으니 주소 대소만 보면 되고, 분기 하나가 줄어든다.

### word 단위 복사와 정렬 위상

바이트 루프는 1바이트당 로드 1 + 스토어 1이고 4바이트씩 옮기면 접근 횟수가 1/4이 된다. 조건은 **정렬 위상이 같아야** 한다는 것이다.

```
  위상 같음 dst%4==src%4: dst=0x..01, src=0x..05 → head 3B면 둘 다 4의 배수 → OK
  위상 다름 dst%4!=src%4: dst=0x..00, src=0x..01 → head k면 dst%4=k, src%4=(1+k)%4
                          둘이 동시에 0이 되는 k가 없다 → 전부 바이트

   [head 0~3B 바이트] [body 4B 단위] [tail 0~3B 바이트]
```

Cortex-M0/M0+(ARMv6-M)는 unaligned `LDR`/`STR`가 **HardFault**다. M3/M4/M7(ARMv7-M)은 `LDR`/`STR`의 unaligned를 허용하지만 `LDM`/`STM`/`LDRD`는 여전히 안 되고 `SCB->CCR`의 `UNALIGN_TRP`를 켜면 전부 트랩이다. DMA 엔진은 거의 항상 전송 폭만큼의 정렬을 요구한다 — "x86에서 잘 돌았다"가 근거가 못 되는 이유다.

## 5. restrict와 strict aliasing — 컴파일러와 맺는 두 계약

```c
void *memcpy(void * restrict dst, const void * restrict src, size_t n);
```

`restrict`는 "이 함수가 도는 동안 `dst`로 접근하는 메모리와 `src`로 접근하는 메모리는 서로 다르다"는 **caller의 약속**이고, 깨면 undefined behaviour다. 핵심 오해 하나. `restrict`는 "겹침을 검사하겠다"가 아니라 **"검사하지 않겠다"**는 선언이다. 그 약속을 근거로 컴파일러가 로드·스토어 순서를 바꾸거나 벡터화하므로, 겹친 인자로 부르면 "조금 이상한 결과"가 아니라 **최적화 수준마다 다른 결과**가 나온다.

strict aliasing은 다른 계약이다. 같은 메모리를 호환되지 않는 타입의 lvalue로 접근하면 undefined behaviour다(문자 타입으로 읽는 것만 예외).
```c
/* 위험: float의 비트를 uint32_t로 보려는 전형적 관용구 */
uint32_t bits = *(uint32_t *)&f;          /* strict aliasing 위반 */

/* 안전: memcpy는 표준이 허용하는 타입 재해석 경로 */
memcpy(&bits, &f, sizeof bits);           /* 컴파일러가 같은 명령으로 접는다 */
```

`unsigned char` 배열을 `uint32_t *`로 읽는 word 복사(문제 10의 `my_memcpy_words`)도 엄격히는 이 회색지대다. 실제 libc가 모두 그렇게 구현되어 있고 모든 실무 툴체인에서 동작하지만 근거는 표준이 아니라 관례다. 그래서 커널과 대부분의 펌웨어 빌드가 `-fno-strict-aliasing`을 켠다. 면접에서는 **"동작한다"와 "표준이 보장한다"를 구분해 말하는 것**이 정답이다.

## 6. 구조체를 와이어에 실을 때 — 왜 캐스팅이 안 되는가

```c
/* 하지 말 것 */
const sensor_sample_t *s = (const sensor_sample_t *)rx_buf;
```

깨지는 이유가 셋이고, **개발 PC에서 재현되는 것은 하나도 없다.**

| 위험 | 무엇이 어긋나나 | x86 개발 PC | 타깃 |
|---|---|---|---|
| **정렬** | `buf`가 4의 배수가 아니면 32비트 필드 접근이 unaligned | 조용히 동작(느려짐만) | ARMv6-M은 HardFault, ARMv7-M은 `UNALIGN_TRP`에 따라 트랩 |
| **엔디언** | 와이어는 big-endian, 호스트는 little-endian | 값이 뒤집힘 | 같음 |
| **padding** | 와이어 10B에는 없는 빈칸을 구조체가 기대 | 다른 필드를 읽음 | 같음 |
| **ABI 변경** | 컴파일러·타깃·옵션이 바뀌면 오프셋이 변함 | 빌드 때 알 수 없음 | 같음 |

```
  와이어 10B  :  id │  seq  │   timestamp_us  │ temp  │flg
  오프셋      :  0    1   2   3   4   5   6    7   8    9
  구조체(12B) :  id │PAD│  seq  │  timestamp_us │ temp │flg│PAD
  오프셋      :  0    1   2   3   4   5   6   7   8   9  10  11
                     ↑ 와이어 seq의 상위 바이트가 여기로. 모든 필드가 밀리고
                       11바이트째는 버퍼 밖이다
```

### packed는 해결이 아니다

`sizeof`가 10이 되고 오프셋은 맞는다. 하지만 **엔디언은 그대로 남고**, `&s->timestamp_us`의 타입은 `uint32_t *`인데 실제 주소가 4의 배수가 아닐 수 있다. 그 포인터를 다른 함수에 넘기면 그 함수는 정렬을 신뢰하고 접근한다 — GCC의 `-Waddress-of-packed-member`가 경고하는 상황이다. 게다가 컴파일러 확장이라 이식성이 없다.

### 안전한 패턴은 하나 — 명시적 serialize / deserialize

```c
out[0] = s->id;
out[1] = (uint8_t)(s->seq >> 8);            /* MSB 먼저 */
out[2] = (uint8_t)(s->seq & 0xFFu);
out[3] = (uint8_t)(s->timestamp_us >> 24);  /* ... */
```

바이트 대입과 시프트만 쓰므로 세 문제가 동시에 사라진다. `out`의 정렬을 요구하지 않고, 바이트 순서를 코드가 명시하며, padding이 나갈 자리가 없다. 비용은 필드당 명령 몇 개인데 BLE 한 패킷당 한 번 도는 코드에서는 계측 가능한 비용이 아니다. 조립할 때 함정 하나. 각 바이트를 먼저 `uint32_t`로 올려야 한다 — `ts = in[0] << 24 | ...`는 `uint8_t`가 `int`로 승격된 뒤 부호 비트를 침범해 UB이고, `ts = ((uint32_t)in[0] << 24) | ...`가 안전하다.

## 7. 엔디언 — 메모리에만 있고 레지스터에는 없다

레지스터 안의 `0x01020304`에는 엔디언이 없다. 엔디언은 그것을 메모리에 늘어놓는 순서의 문제다.

```
  uint32_t v = 0x01020304;  를 0x2000에 저장

  little-endian (ARM 기본, x86)     big-endian (네트워크 순서)
   0x2000: 04 03 02 01              0x2000: 01 02 03 04
```

실무 규칙은 단순하다. **와이어 포맷은 코드가 명시하고, 호스트 엔디언은 아예 모르는 채로 쓴다.** 시프트로 조립·분해하면 같은 소스가 어느 호스트에서도 같은 바이트를 만든다. `htons`/`ntohl`이나 `__builtin_bswap32`는 호스트 엔디언을 알아야 하는 방식이라 크로스 플랫폼 프로토콜 코드에서는 시프트가 더 안전하다. 부호 있는 필드에는 함정이 하나 더 있다. `(int16_t)u` 캐스트만 쓰면 범위를 넘는 값의 결과가 C11에서 implementation-defined다(C23부터 2의 보수로 고정). 표준만 보고도 답이 하나이려면 이렇게 쓴다.

```c
int16_t u16_to_i16(uint16_t u)
{
    return (u & 0x8000u) ? (int16_t)((int32_t)u - 65536) : (int16_t)u;
}
```

## 8. volatile이 보장하는 것 — 정확한 문장으로

`volatile` 객체에 대한 접근은 C 추상 기계의 **관측 가능한 부수효과**다. 컴파일러는 그 부수효과를 **없애거나, 합치거나, 개수를 바꾸거나, 다른 volatile 접근과 순서를 바꿀 수 없다.** 소스에 읽기가 세 번이면 기계어에도 세 번 있어야 한다. (정확히는 "무엇이 volatile 접근을 구성하는가"를 표준이 implementation-defined로 남겨 두었다. 세부는 컴파일러 문서가 정하고, 실무에서는 위 문장대로 구현되어 있다.)

하드웨어가 5틱 뒤에 `READY`를 세우는 상황에서 문제 12의 두 폴링 함수를 나란히 놓으면 이렇다.

```
  매 반복 새로 읽는 버전            읽기를 루프 밖으로 뺀 버전
  poll 1: read → 0   tick          cached = read → 0
  poll 5: read → 0   tick (세워짐)  poll 1..100: cached=0   tick
  poll 6: read → 1 → 성공          → 타임아웃
  읽기 6회, 결과 0                  읽기 1회, 결과 -1 ← 레지스터엔 READY가 서 있다
```

오른쪽은 억지 코드가 아니다. 대상이 `volatile`이 **아니라면** 컴파일러가 정당하게 수행할 수 있는 변형(loop-invariant code motion)이다. 증상은 언제나 **타임아웃 아니면 행(hang)**이고, 최적화를 낮추면 사라지므로 "디버그 빌드에서는 되는데 릴리스에서 안 된다"의 전형적 원인이다. 쓰기도 같다. write-1-to-clear 레지스터에 같은 값을 두 번 쓰는 것은 **두 번의 명령**이고, 컴파일러가 중복 저장으로 보고 하나를 지우면 하드웨어 동작이 사라진다. 레지스터 쓰기는 대입이 아니라 명령이다.

### volatile이 보장하지 않는 것

| 착각 | 실제 | 무엇으로 해결하나 |
|---|---|---|
| **원자성** | `volatile uint32_t c; c++;`은 여전히 읽기·증가·쓰기 세 단계. 중간에 끼어들면 갱신이 사라진다 | critical section, `_Atomic`, 하드웨어의 set-only/clear-only 레지스터 |
| **메모리 배리어** | volatile끼리는 순서가 지켜지지만 **비-volatile 접근**은 그 사이로 옮겨질 수 있고, 쓰기 버퍼와 버스도 재정렬한다 | `DMB`/`DSB`, `atomic_thread_fence` |
| **스레드 안전성** | C11에서 동기화 없는 다중 스레드 접근은 volatile이 있어도 data race = UB | 락, `_Atomic`, 단일 생산자-단일 소비자 설계 |
| **캐시 일관성** | 멀티코어 캐시 일관성은 하드웨어나 명시적 cache maintenance의 일이다 | cache clean/invalidate |

RMW가 원자적이 아니라는 것의 타임라인이다.

```
  ctrl = ENABLE
  CPU (task)                       ISR
  ─────────────────────            ───────────────────────────
  [1] v = reg_read(&ctrl)  ENABLE
                                   [2] reg_set_bits(&ctrl, IRQ_EN)
                                       ctrl = ENABLE | IRQ_EN
  [3] v |= LOOPBK
      reg_write(&ctrl, v)
      ctrl = ENABLE | LOOPBK           ↑ ISR이 세운 IRQ_EN이 사라졌다
```

`volatile`은 [1]과 [3]의 접근이 실제로 일어나도록만 보장했고 틈은 그대로다. 고치는 방법 셋. RMW를 critical section으로 감싸거나(인터럽트 지연시간이 늘어난다), 하드웨어의 set-only/clear-only 레지스터를 쓰거나(가능하면 항상 이쪽), `_Atomic`을 쓴다(주변장치 레지스터엔 대개 못 쓴다).

## 9. const와 const volatile — 두 한정자는 다른 말을 한다

- `const`: **이 포인터로는 쓰지 않는다.** 컴파일러가 강제하고, 위반하면 컴파일 에러다.
- `volatile`: **값이 이 프로그램 밖에서 바뀔 수 있다.** 접근을 줄이지 말라는 지시다.

| 선언 | 의미 | 예 |
|---|---|---|
| `uint32_t *` | 평범한 변수 | SRAM 버퍼 |
| `const uint32_t *` | 내가 안 쓴다, 값도 안 변한다 | flash 룩업 테이블 |
| `volatile uint32_t *` | 읽고 쓴다, 값이 변한다 | 제어 레지스터(`CTRL`) |
| `const volatile uint32_t *` | 내가 쓰면 컴파일 에러, 값은 계속 변한다 | 상태 레지스터(`STATUS`) |

`const`만 붙이면 컴파일러가 "안 변한다"고 판단해 읽기를 한 번으로 줄일 수 있다. 상태 레지스터에 `const`만 붙이면 정확히 §8의 스냅샷 폴링이 된다. 그래서 읽기 전용 레지스터는 **반드시 `const volatile`**이다.

```c
uint32_t reg_read(const volatile uint32_t *reg) { return *reg; }
/*                ↑ 쓰기 금지     ↑ 읽기를 줄이지 말 것 */
```

그리고 `const volatile uint32_t *p`(가리키는 대상이 const volatile)와 `uint32_t * const p`(포인터 자체가 const)는 완전히 다르다 — 선언은 이름에서 시작해 바깥으로 읽는다.

## 10. 함수 포인터와 디스패치 테이블

`switch`는 코드고 테이블은 데이터다. 명령이 통신 프로토콜에서 오면 이 차이가 크다.

| 축 | switch | 테이블 |
|---|---|---|
| 명령 추가 | 함수를 고친다 | 행 하나 추가 |
| 배치 | `.text` | `.rodata`(flash). RAM 0바이트 |
| 조회 | 점프 테이블로 접히면 O(1), 흩어진 값이면 비교 사슬 | 선형 O(n) 또는 역인덱스로 O(1) |
| 메타데이터 | 없다 | 이름·인자 개수·권한을 같은 행에 |
| 검증 | 불가능 | 부팅 때 전수 검사 |
| 위험 | 없음 | 함수 포인터가 NULL이면 주소 0으로 점프 |

마지막 두 줄이 짝을 이룬다. 주소 0으로 점프한 HardFault의 스택에는 "왜 여기 왔는지" 단서가 거의 없어서, 테이블에는 검증 함수가 따라붙는다.

```
  부팅: assert(cmd_table_validate(tbl,n) == 0)
        → fn == NULL · name == NULL · min_args > max_args · opcode 중복
  런타임 (프레임마다): [opcode][len][payload]
        cmd_find(opcode) ─없음──→ CMD_ERR_UNKNOWN
        fn == NULL ? ───그렇다─→ CMD_ERR_NULL_FN
        nargs 범위? ───아니오─→ CMD_ERR_NARGS
        fn(args, nargs, ctx)        ← 여기까지 와야 ctx가 바뀐다
```

**opcode 중복**이 특히 나쁜 부류다. 컴파일러도 링커도 못 잡고 뒤 행이 조용히 죽은 코드가 된다. 증상은 "그 명령이 가끔 엉뚱하게 동작한다"이고 원인은 소스를 눈으로 훑어야 보인다 — 부팅 때 전수 검사가 이걸 잡는다.

### 콜백과 문맥 — `void *ctx`가 있는 이유

핸들러가 전역을 만지면 인스턴스가 하나일 때만 옳다. 마이크가 두 개가 되면 전부 다시 써야 한다.

```c
typedef int (*cmd_fn)(const uint8_t *args, size_t nargs, void *ctx);

cmd_dispatch(tbl, n, SET_GAIN, &g10, 1, &left);   /* left.gain  = 10 */
cmd_dispatch(tbl, n, SET_GAIN, &g50, 1, &right);  /* right.gain = 50 */
```

디스패처는 `ctx`를 **해석하지 않고** `ctx == NULL` 검사는 핸들러가 한다. 이 경계를 지키면 디스패처가 어떤 서브시스템에도 재사용된다. 같은 원리로 **개수 검사는 디스패처, 값 범위 검사는 핸들러**다 — "gain은 0~64"라는 지식은 `h_set_gain` 안에만 있어야 한다. 오류 코드 영역도 나눈다. 핸들러는 `-1 ~ -15`, 디스패처는 `-16` 이하. 호출자가 반환값만 보고 "프로토콜이 틀렸는지(재전송해도 소용없다), 값이 틀렸는지(사용자에게 알린다)"를 구분할 수 있다.

선언 읽기는 괄호 하나가 갈린다. `int (*fp)(void)`는 함수 포인터, `int *fp(void)`는 `int *`를 반환하는 함수, `int (*fpa[4])(void)`는 함수 포인터 4개 배열이다. 그리고 표준 규칙 하나. **함수 포인터와 객체 포인터(`void *`) 사이의 변환은 C 표준이 보장하지 않는다.** POSIX가 `dlsym` 때문에 따로 요구하지만 순수 C에서는 아니다.

---

## 흔한 함정

| 함정 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 수신 버퍼를 구조체로 캐스팅 | 타깃에서만 값이 틀림, 또는 HardFault | 정렬 + 엔디언 + padding이 동시에 어긋남 | 바이트 단위 serialize/deserialize |
| `sizeof` = 멤버 합이라 가정 | 프레임 길이가 2~6바이트 어긋남 | padding을 안 셈 | 와이어 크기를 별도 상수로 + `_Static_assert` |
| `memcmp`로 구조체 비교 | 같은 값인데 다르다고 나옴 | padding 바이트 값이 불특정 | 필드 단위 비교 함수 |
| 지역 변수 주소를 리턴 | 한동안 잘 돌다 어느 호출에서 터짐 | 스택 프레임 수명 종료 | caller가 버퍼를 주거나 static/pool |
| `void *`에 `+ 1` / 다른 객체 주소를 `<`로 비교 | 빌드 실패, 또는 최적화에서 이상 동작 | 표준 C가 아닌 GNU 확장 · unspecified | `unsigned char *`로 걷기 · `uintptr_t`로 비교 |
| `*(uint32_t *)&float_var` | `-O2`에서만 값이 틀림 | strict aliasing 위반 | `memcpy`로 재해석 |
| `restrict` 함수에 겹친 인자 | 최적화 수준마다 결과가 다름 | caller가 약속을 깼다 | `memmove` 쓰기 |
| 상태 레지스터에 `const`만 | 디버그는 되고 릴리스에서 행 | 읽기가 하나로 합쳐짐 | `const volatile` |
| `volatile` 카운터를 `++` | 드물게 값이 하나 빠짐 | RMW가 원자적이 아님 | critical section 또는 `_Atomic` |
| `volatile`을 배리어로 착각 | DMA가 낡은 데이터를 보냄 | 비-volatile 재정렬 + 캐시 | `DMB`/`DSB` + cache clean/invalidate |
| 테이블에 NULL 핸들러 행 | 주소 0으로 점프, 단서 없는 HardFault | 검증 없음 | 부팅 때 `cmd_table_validate` |
| opcode 중복 | 그 명령이 가끔 엉뚱하게 동작 | 뒤 행이 죽은 코드가 됨 | 부팅 때 전수 중복 검사 |
| `in[0] << 24` (`uint8_t`) | 최상위 바이트가 이상한 값 | int 승격 후 부호 비트 침범 = UB | `((uint32_t)in[0] << 24)` |

---

## 면접 질문 10개

### Q1. `volatile`이 무엇을 보장하는지 한 문장으로.

volatile 객체 접근은 관측 가능한 부수효과라서, 컴파일러가 그 접근을 없애거나 합치거나 개수를 바꾸거나 다른 volatile 접근과 순서를 바꿀 수 없습니다. 소스에 읽기가 세 번이면 기계어에도 세 번 있어야 한다는 뜻이고, 그게 전부입니다. 최적화를 전부 끄는 것이 아니라 그 객체에 대한 접근 횟수와 순서만 고정합니다.

> Volatile means every access in the source becomes a real access in the generated code — the compiler may not elide, merge, or reorder them relative to other volatile accesses.

### Q2. `volatile`이 보장하지 않는 것 세 가지.

원자성, 메모리 배리어, 스레드 안전성입니다. `volatile uint32_t`의 `++`는 여전히 읽기·증가·쓰기 세 단계라서 중간에 끼어들면 갱신이 사라집니다. 비-volatile 접근은 volatile 접근 사이로 재정렬될 수 있고 CPU의 쓰기 버퍼와 버스도 재정렬합니다. C11 기준으로 동기화 없는 다중 스레드 접근은 volatile이 있어도 data race라 undefined behaviour입니다. 각각 critical section, `DMB`/`DSB`, 락이나 `_Atomic`으로 해결합니다.

> Volatile gives you access ordering, not atomicity, not a memory barrier, and not thread safety.

### Q3. `const volatile`은 모순 아닙니까?

아니고, 읽기 전용 레지스터가 정확히 그것입니다. `const`는 "내가 이 포인터로 쓰지 않겠다"는 컴파일러와의 약속이고 `volatile`은 "값이 내 프로그램 밖에서 바뀐다"는 사실입니다. 두 한정자는 직교합니다. 상태 레지스터에 `const`만 붙이면 컴파일러가 값이 안 변한다고 판단해 읽기를 한 번으로 줄일 수 있어서 폴링 루프가 영원히 같은 값을 보게 됩니다.

> A status register is read-only to the CPU but changes on its own, so it is const volatile — const stops me from writing, volatile stops the compiler from caching.

### Q4. `sizeof`가 멤버 크기의 합과 다른 이유.

각 멤버가 자기 정렬의 배수인 오프셋에 놓여야 하고, 구조체 전체 크기는 가장 큰 멤버 정렬의 배수로 올림되기 때문입니다. `uint8_t` 다음에 `uint16_t`가 오면 1바이트가 버려지고, 전체를 4의 배수로 맞추느라 뒤에 또 버려집니다. 후미 padding이 필요한 이유는 배열을 만들 수 있어야 하기 때문입니다. `sizeof`가 정렬의 배수가 아니면 `arr[1]`부터 정렬이 깨집니다.

> Members are placed at naturally aligned offsets and the total size is rounded up to the struct's alignment, so arrays of it stay aligned.

### Q5. 수신 버퍼를 구조체 포인터로 캐스팅하면 무엇이 깨집니까?

세 가지가 동시에 깨집니다. 정렬은 버퍼 시작이 4의 배수가 아니면 32비트 필드 접근이 unaligned가 되어 ARMv6-M에서 HardFault입니다. 엔디언은 와이어가 big-endian인데 ARM이 little-endian이라 값이 뒤집힙니다. padding은 와이어에 없는 빈칸을 구조체가 기대해서 모든 필드가 밀려 읽힙니다. 문제는 세 가지 다 x86 개발 PC에서는 재현되지 않거나 눈에 안 띈다는 점입니다.

> Casting a wire buffer to a struct pointer bets on alignment, endianness, and padding all matching — and none of those bets are checked by the compiler.

### Q6. `memcpy`와 `memmove`의 구현상 차이는?

복사 방향 하나입니다. `memmove`는 `dst`와 `src`의 주소 대소를 보고 `dst < src`면 앞에서부터, `dst > src`면 뒤에서부터 복사합니다. 겹침 여부는 검사하지 않습니다. 겹치지 않으면 어느 방향이든 결과가 같으니 분기 하나를 줄이는 겁니다. `memcpy`는 `restrict`로 겹치지 않는다는 약속을 받았기 때문에 방향을 고민하지 않고 가장 빠른 순서로 옮길 수 있습니다.

> Memmove just picks the copy direction from the address order; memcpy is allowed to skip that because restrict promises the regions do not overlap.

### Q7. `restrict`는 누가 무엇을 약속하는 겁니까?

호출하는 쪽이 약속합니다. "이 함수가 도는 동안 이 두 포인터로 접근하는 메모리는 서로 다르다"입니다. 구현이 겹침을 검사하겠다는 뜻이 아니라 검사하지 않겠다는 선언이라서 약속을 깨면 undefined behaviour입니다. 컴파일러가 그 약속을 근거로 로드·스토어 순서를 바꾸거나 벡터화하기 때문에, 겹친 인자로 부르면 조금 이상한 결과가 아니라 최적화 수준마다 다른 결과가 나옵니다.

> Restrict is a promise made by the caller, not a check made by the callee — break it and the behaviour is undefined, not merely wrong.

### Q8. word 단위 복사가 가능한 조건은?

두 주소의 정렬 위상이 같아야 합니다. `dst % 4 == src % 4`일 때만 head 몇 바이트를 바이트로 옮겨 둘을 동시에 4의 배수로 만들 수 있습니다. 위상이 다르면 어떤 head 길이로도 둘이 동시에 정렬되지 않아 바이트 복사로 떨어져야 합니다. 그리고 길이가 최소 두 word는 되어야 head/tail 처리 비용을 회수합니다.

> You can only go word-at-a-time when both pointers share the same alignment phase, because otherwise no head length aligns them both.

### Q9. 왜 `switch` 대신 디스패치 테이블입니까? 대가는?

명령이 통신 프로토콜에서 오면 테이블이 데이터가 되기 때문입니다. 명령 추가가 행 하나로 끝나고, `const`로 두면 flash에 놓여 RAM을 안 쓰고, 이름과 인자 개수 같은 메타데이터를 같은 행에 둘 수 있고, 무엇보다 부팅 때 테이블 전체를 검증할 수 있습니다. 대가는 함수 포인터가 잘못되면 아무 주소로 점프한다는 점입니다. 그래서 NULL 핸들러와 중복 opcode를 부팅 때 assert로 잡습니다.

> A table turns the protocol into data you can validate at boot, at the cost of function pointers that must be checked before they are called.

### Q10. 콜백에 `void *ctx`를 왜 같이 넘깁니까?

핸들러가 전역 상태를 보지 않게 하기 위해서입니다. 같은 핸들러 코드로 여러 인스턴스를 구동할 수 있어야 하는데, 마이크가 두 개가 되면 전역 변수 방식은 전부 다시 써야 합니다. 디스패처는 `ctx`를 해석하지 않고 그냥 넘기고 `ctx == NULL` 검사는 핸들러가 합니다. 그 경계를 지키면 디스패처가 다른 서브시스템에도 그대로 재사용됩니다.

> The context pointer keeps handlers free of globals, so one table can drive two device instances without touching the handler code.

---

## 이 레벨 문제들

| N | 문제 | 무엇을 연습하나 | 난이도 | 목표 | 채점 |
|---|---|---|---|---|---|
| **10** | [memcpy_move](../problems/10_memcpy_move.html) | 겹침 판정, 복사 방향, 정렬 위상, `restrict` 계약 | 기초 | 25분 | `make run N=10` |
| **11** | [struct_layout](../problems/11_struct_layout.html) | 패딩 계산, `offsetof` + `_Static_assert`, 와이어 직렬화 | 중급 | 30분 | `make run N=11` |
| **12** | [volatile_const](../problems/12_volatile_const.html) | 접근 횟수 계측, 폴링 루프, RMW 비원자성, `const volatile` | 중급 | 25분 | `make run N=12` |
| **13** | [func_table](../problems/13_func_table.html) | 디스패치 테이블, 테이블 검증, 콜백 + 문맥 | 기초 | 25분 | `make run N=13` |

이웃 레벨은 [L0 비트·정수 기초](L0_bit_basics.html)(특히 08 endian_align의 정렬 올림과 unaligned 접근)와 [L2 임베디드 관용구](L2_embedded_idioms.html)다. [04 memory pool](04_mem_pool.html)의 intrusive free list도 이 레벨의 정렬 이야기와 직결된다.

---

## 체크리스트

- [ ] 구조체 오프셋·padding을 종이에서 계산하고, 후미 padding이 왜 필요한지 배열로 설명할 수 있다
- [ ] 필드 순서를 바꿔 `sizeof`를 줄이고 줄어든 이유를 말할 수 있다
- [ ] `offsetof` + `_Static_assert`로 숫자를 박지 않고 가정을 고정할 수 있다
- [ ] 수신 버퍼 캐스팅이 깨지는 세 이유를 대고, x86에서 재현되는 것을 구분할 수 있다
- [ ] `memmove`가 방향만 바꾸는 이유와 겹침 검사를 생략해도 되는 이유를 말할 수 있다
- [ ] 앞방향 복사의 되풀이 패턴 주기를 예측하고, 정렬 위상이 다를 때 word 복사가 불가능한 이유를 주소로 보일 수 있다
- [ ] `restrict`를 "누가 누구에게 하는 약속"으로 설명하고, strict aliasing 위반에 `memcpy` 대안을 낼 수 있다
- [ ] `volatile`의 보장을 한 문장으로, 비보장 세 가지를 예와 함께 말하고 RMW 비원자성을 task/ISR 타임라인으로 그릴 수 있다
- [ ] 디스패치 테이블의 검증 항목 네 가지를 대고, `void *ctx` 없는 콜백이 어디서 깨지는지 말할 수 있다
- [ ] 10~13 네 문제를 전부 `make run`으로 통과시켰다
