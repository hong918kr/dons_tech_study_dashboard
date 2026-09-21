# 03. 레지스터 비트필드 매크로와 read-modify-write — 해설

> **문제**: [problems/03_reg_bitfield.md](../problems/03_reg_bitfield.html) · **답안**: `solutions/03_reg_bitfield.c`
> **이 노트를 다 읽으면**: (1) 마스크·shift 매크로를 비트 그림으로 유도할 수 있다. (2) read-modify-write가 안전한 레지스터와 그게 곧 버그인 레지스터를 구분할 수 있다. (3) `volatile`이 보장하는 것과 보장하지 않는 것을 한 문장으로 말할 수 있다.

## 0. 한 문장으로

레지스터 필드 조작은 "마스크를 만들고(GENMASK), 값을 자리로 올리고(FIELD_PREP), 읽고-지우고-얹어서 한 번만 쓴다(reg_update)"의 세 동작이 전부지만, write-1-to-clear 레지스터와 ISR 경쟁에서는 이 마지막 한 번의 쓰기가 남의 인터럽트를 지워 버린다.

---

## 1. 왜 이 패턴이 필요한가 — 하드웨어에서 출발

MCU에서 주변장치는 메모리 주소 공간에 뚫려 있다. `0x40004000`을 읽으면 RAM이 아니라 UART 블록 안의 플립플롭 묶음을 읽는 것이다.

```text
   CPU core                  AHB/APB bus             peripheral
 +-------------+          +-----------------+    +------------------------+
 | LDR r0,[r1] | --addr-> | address decoder | -> | UART0 0x40004000       |
 | STR r0,[r1] | <--data- |                 | <- | +0x00 CTRL R/W         |
 +-------------+          +-----------------+    | +0x04 STATUS RO+W1C    |
                                                 | +0x08 DATA R/W         |
                                                 +------------------------+
```

여기서 생기는 세 가지 사실이 이 문제의 전부다.

1. 접근 단위는 **워드 하나**다. "bit 19..8만 쓰기" 같은 버스 트랜잭션은 없다. 필드 하나를 바꾸려면 워드를 읽고, 고치고, 워드를 다시 써야 한다. 이것이 read-modify-write(RMW)다.
2. 레지스터는 **CPU 모르게 값이 바뀐다**. UART가 바이트를 받으면 STATUS의 RXNE가 1이 된다. 컴파일러는 이를 알 수 없으므로 `volatile`로 "이 주소는 매번 진짜로 읽어라"라고 알려 줘야 한다.
3. 비트마다 **쓰기 의미가 다르다**. 보통 비트는 쓴 값이 그대로 남지만, 에러 플래그는 1을 쓰면 지워지고(W1C), read-only 비트는 뭘 써도 무시되고, reserved 비트는 "읽은 값을 그대로 되써라"가 규칙이다.

이번 문제의 예시 UART 레이아웃은 이렇게 생겼다(주소·배치 모두 설명용 가짜다).

```text
UART CTRL  (offset 0x00, R/W)
 31        24 23   20 19         8  7   6   5  4  3  2  1  0
+------------+-------+------------+---+---+------+--+--+--+--+
|  reserved  |RXTRIG |  BAUDDIV   |RTS|ST2|PARITY|LB|RX|TX|EN|
+------------+-------+------------+---+---+------+--+--+--+--+
   보존       4bit      12bit       1   1    2bit  1  1  1  1

UART STATUS (offset 0x04)
 31      12  11  10   9    8   7    4  3   2     1     0
+----------+---+----+----+----+------+---+-----+-----+-----+
| reserved |BRK|PERR|FERR|OVR |RXLVL |rsv|TXEMP|RXNE |TXRDY|
+----------+---+----+----+----+------+---+-----+-----+-----+
            \_____ W1C _____/   \________ read-only ______/
```

---

## 2. 먼저 그림으로 이해하기

### 2.1 GENMASK(hi, lo)를 유도하기

아래 그림은 모두 32비트를 4비트씩 끊어 그렸고 맨 윗줄 숫자는 각 묶음의 최상위 비트 번호다. 목표는 bit 19..8만 1인 마스크다. "위에서 자르고, 아래에서 자른다"를 두 번의 shift로 만든다.

```text
        31   27   23   19   15   11   7    3
all     1111 1111 1111 1111 1111 1111 1111 1111   0xFFFFFFFF
>>12    0000 0000 0000 1111 1111 1111 1111 1111   0x000FFFFF   (31 - hi = 12)
<<8     1111 1111 1111 1111 1111 1111 0000 0000   0xFFFFFF00   (lo = 8)
AND     0000 0000 0000 1111 1111 1111 0000 0000   0x000FFF00   = GENMASK(19,8)
```

`0xFFFFFFFFu >> (31u - hi)`는 bit 31..hi+1을 잘라 내고, `0xFFFFFFFFu << lo`는 bit lo-1..0을 잘라 낸다. 둘을 AND하면 원하는 구간만 남는다.

왜 `(BIT(hi+1) - BIT(lo))`처럼 안 쓰나. `hi == 31`이면 `BIT(32)`가 되고, 32비트 타입을 32비트 이상 shift하는 것은 정의되지 않은 동작(UB)이다. 위 방식은 `hi == 31`일 때 `>> 0`이 되어 안전하다. `FIELD_MASK(width, shift)`도 같은 논리다.

```text
FIELD_MASK(12, 8) = (0xFFFFFFFF >> (32 - 12)) << 8
                  = 0x00000FFF << 8
                  = 0x000FFF00                       (= GENMASK(19, 8))
```

`width == 32`일 때 `>> 0`이 되도록 `32 - width`를 쓴다. 대신 `width == 0`은 금지다(shift 32 = UB). 그래서 요구사항에 `1 <= width <= 32`를 못박았다.

### 2.2 FIELD_PREP: 값을 자리로 올리기

BAUDDIV 필드(bit 19..8)에 `0x0D1`을 넣는다.

```text
        31   27   23   19   15   11   7    3
val     0000 0000 0000 0000 0000 0000 1101 0001   0x000000D1
<<8     0000 0000 0000 0000 1101 0001 0000 0000   0x0000D100   (shift = ctz(mask) = 8)
&mask   0000 0000 0000 0000 1101 0001 0000 0000   0x0000D100
```

shift량은 마스크에서 뽑는다. `__builtin_ctz(mask)`는 최하위 1비트까지의 0 개수, 즉 필드의 시작 비트 번호다. 상수 마스크면 컴파일 타임에 8로 접힌다.

`& mask`가 왜 필요한가. 값이 필드 폭을 넘으면 이웃 필드를 덮어쓰기 때문이다. RXTRIG 필드(bit 23..20, 4비트)에 실수로 `0x1F`를 넣어 보자.

```text
        31   27   23   19   15   11   7    3
val     0000 0000 0000 0000 0000 0000 0001 1111   0x0000001F
<<20    0000 0001 1111 0000 0000 0000 0000 0000   0x01F00000   <-- bit24가 켜졌다(reserved!)
mask    0000 0000 1111 0000 0000 0000 0000 0000   0x00F00000
&mask   0000 0000 1111 0000 0000 0000 0000 0000   0x00F00000   <-- 안전하게 잘림
```

`& mask`가 없으면 `reg_update`의 `val & mask`에서 어차피 잘리지만, 매크로 자체가 잘라 주면 `FIELD_PREP(A,x) | FIELD_PREP(B,y)`처럼 값을 조립하는 경로에서도 오염이 없다. 잘림을 **버그로 잡고 싶을 때**는 `FIELD_FITS`로 `assert`한다.

### 2.3 FIELD_GET: 값을 꺼내기

```text
        31   27   23   19   15   11   7    3
reg     1101 1110 1010 1101 0001 1010 0011 1111   0xDEAD1A3F
mask    0000 0000 0000 1111 1111 1111 0000 0000   0x000FFF00
&       0000 0000 0000 1101 0001 1010 0000 0000   0x000D1A00
>>8     0000 0000 0000 0000 0000 1101 0001 1010   0x00000D1A   = 필드 값 0xD1A
```

### 2.4 reg_update: 읽고-지우고-얹고-쓰기

BAUDDIV를 `0x111`에서 `0x222`로 바꾼다. 나머지 비트는 전부 살아남아야 한다.

```text
        31   27   23   19   15   11   7    3
before  1010 0101 0011 0001 0001 0001 1100 0111   0xA53111C7   (1) bus read 1회
~mask   1111 1111 1111 0000 0000 0000 1111 1111   0xFFF000FF
cleared 1010 0101 0011 0000 0000 0000 1100 0111   0xA53000C7   (2) 필드 자리만 비운다
prep    0000 0000 0000 0010 0010 0010 0000 0000   0x00022200   (3) 새 값
after   1010 0101 0011 0010 0010 0010 1100 0111   0xA53222C7   (4) bus write 1회
        \___/ \_/ \__/ \______________/ \_______/
       rsvd=A5 RXTRIG=3   BAUDDIV=0x222  RTS,ST2,EN,TX,RX 그대로
```

reserved `0xA5`, RXTRIG `3`, 아래쪽 플래그들이 전부 그대로다. 이것이 "이웃을 건드리지 않는 다중 비트 필드 갱신"이다.

### 2.5 uart_config 전체 타임라인

```text
        31   27   23   19   15   11   7    3
before  1010 0101 0000 0000 0000 0000 0000 0000   0xA5000000   reserved만 세팅된 상태
mask    0000 0000 1111 1111 1111 1111 0111 0000   0x00FFFF70   RXTRIG|BAUDDIV|ST2|PARITY
val     0000 0000 1000 0000 1101 0001 0110 0000   0x0080D160   8, 0x0D1, ST2=1, PAR=2
step2   1010 0101 1000 0000 1101 0001 0110 0000   0xA580D160   reg_update 후
step3   1010 0101 1000 0000 1101 0001 0110 0111   0xA580D167   EN|TXE|RXE 올린 뒤
```

순서가 (1) EN 내리기 (2) 필드 교체 (3) EN 올리기인 이유는, 대부분의 IP가 "동작 중에 분주비를 바꾸면 그 순간 나가던 프레임이 깨진다"라고 명시하기 때문이다. 데이터시트에 그런 문장이 있으면 드라이버는 반드시 disable 후 설정한다.

---

## 3. 잘못된 구현부터 보기

### 3.1 순진한 구현 1 — "필요한 비트만 OR로 켠다"

```c
/* 틀림 */
*CTRL |= (0x222u << 8);      /* BAUDDIV = 0x222 로 만들고 싶었다 */
```

```text
        31   27   23   19   15   11   7    3
before  1010 0101 0011 0001 0001 0001 1100 0111   BAUDDIV = 0x111
new     0000 0000 0000 0010 0010 0010 0000 0000   넣고 싶은 값
OR      1010 0101 0011 0011 0011 0011 1100 0111   BAUDDIV = 0x333  <-- 섞였다
```

OR는 1만 세울 수 있고 0으로 내릴 수 없다. 다중 비트 필드는 **먼저 지워야** 한다. 이게 `(r & ~mask) | (val & mask)`의 존재 이유다.

### 3.2 순진한 구현 2 — "그냥 통째로 쓴다"

```c
/* 틀림 */
*CTRL = FIELD_PREP(UART_CTRL_BAUDDIV_MASK, 0x222u);
```

reserved 비트 `0xA5000000`이 0이 되고 EN/TXE/RXE도 꺼진다. 벤더 문서의 reserved 설명은 대부분 "read-modify-write 할 것, 읽은 값을 되쓸 것"이고, 통째 쓰기는 칩 리비전이 바뀌어 reserved가 기능 비트가 되는 순간 조용히 깨진다.

### 3.3 순진한 구현 3 — W1C 레지스터에 `|=`

이것이 이 문제의 핵심이고, 실제 필드에서 "인터럽트가 가끔 씹힌다"로 나타나는 버그다.

W1C 하드웨어는 이렇게 동작한다.

```text
             set  (하드웨어: 에러 발생 시 1로 세움)
              |
              v
        +-----------+
   ---> |  D-FF     | ---> CPU가 읽는 값
        +-----------+
              ^
              |
        clear = (CPU가 쓴 값의 그 비트 == 1)
```

즉 **쓴 값 1 = "이 플래그 지워라"**, **쓴 값 0 = "손대지 마라"**이다. 여기에 RMW를 하면 이렇게 된다.

```text
상황: OVERRUN(bit8)과 FRAMEERR(bit9)가 동시에 pending, TXRDY(bit0)는 RO=1

        31   27   23   19   15   11   7    3
pend    0000 0000 0000 0000 0000 0011 0000 0001   0x00000301

[올바른 코드]  uart_clear_errors(hw, UART_ST_OVERRUN)
write   0000 0000 0000 0000 0000 0001 0000 0000   0x00000100   OVR만 1
after   0000 0000 0000 0000 0000 0010 0000 0001   0x00000201   FERR 살아있음

[틀린 코드]  status |= UART_ST_OVERRUN;  (읽은 값 0x301에 OR해서 되씀)
write   0000 0000 0000 0000 0000 0011 0000 0001   0x00000301   OVR도 1, FERR도 1
after   0000 0000 0000 0000 0000 0000 0000 0001   0x00000001   FERR 유실!
                                     ^^
                        처리하지도 않은 FRAMEERR가 사라졌다
```

증상은 이렇게 나타난다. "115200bps로 장시간 돌리면 프레이밍 에러 카운터가 0인데 수신 데이터는 가끔 깨진다." 에러가 안 난 게 아니라, 에러 플래그를 **읽은 적도 없이 지워 버린** 것이다. 인터럽트 컨트롤러의 pending 비트(NVIC ICPR), DMA의 CSR, 타이머의 SR도 전부 같은 함정을 갖는다.

교훈: **W1C 레지스터에서 read-modify-write는 코드 스타일 문제가 아니라 기능 버그다.** 지울 비트만 딱 1회 쓴다.

---

## 4. 한 줄씩 만들기

### 4.1 단계 1 — 폭과 부호를 못박는다

```c
#define BIT(n)                  ((uint32_t)1u << (n))
#define GENMASK(hi, lo)         ((0xFFFFFFFFu >> (31u - (hi))) & (0xFFFFFFFFu << (lo)))
#define FIELD_MASK(width, shift) ((0xFFFFFFFFu >> (32u - (width))) << (shift))
```

`1u`가 아니라 `1`이면 `1 << 31`이 signed int overflow로 UB다. `1UL`이면 ARM(32-bit long)에서는 맞지만 x86-64 host 유닛 테스트에서는 64비트가 되어 `~mask`가 상위 32비트까지 1이 된다. `uint32_t`로 고정하면 두 환경에서 같은 값이 나온다. 이 줄이 없으면 "타깃에서는 되는데 host 테스트만 실패한다"가 생긴다.

### 4.2 단계 2 — shift량을 마스크에서 뽑는다

```c
#define FIELD_SHIFT(mask)       ((uint32_t)__builtin_ctz(mask))
#define FIELD_PREP(mask, val)   (((uint32_t)(val) << FIELD_SHIFT(mask)) & (mask))
#define FIELD_GET(mask, reg)    (((uint32_t)(reg) & (mask)) >> FIELD_SHIFT(mask))
```

필드마다 `_POS`와 `_MSK` 두 상수를 따로 관리하면 언젠가 둘이 어긋난다(CMSIS 헤더의 고질적 오타 원인이다). 마스크 하나만 두고 shift는 계산하면 진실의 원천이 하나가 된다. `__builtin_ctz`는 GCC/Clang 내장이고 상수 인자면 컴파일 타임에 접힌다. `mask == 0`이면 UB이므로 "마스크는 항상 0이 아니다"가 전제다. 이 줄이 없으면 매크로마다 shift 상수를 손으로 써야 하고, 필드가 옮겨졌을 때 한쪽만 고치는 사고가 난다.

### 4.3 단계 3 — RMW를 한 곳에 가둔다

```c
static inline void reg_update(volatile uint32_t *reg, uint32_t mask, uint32_t val)
{
    uint32_t r = *reg;                   /* 1회 read  */
    r = (r & ~mask) | (val & mask);      /* 지우고 얹는다 (비휘발성 지역변수에서) */
    *reg = r;                            /* 1회 write */
}
```

중간 계산을 지역 변수 `r`에서 하는 것이 포인트다. `*reg = (*reg & ~mask) | (val & mask);`라고 쓰면 `volatile` 접근이 **두 번** 일어날 수 있고, 어떤 순서로 읽히는지도 표준이 보장하지 않는다. 레지스터를 두 번 읽으면 FIFO를 pop 하는 레지스터나 read-to-clear 레지스터에서 즉시 사고가 난다. 이 줄이 없으면 "read가 몇 번인지"를 코드만 보고 말할 수 없다. 끝의 `val & mask`는 호출자가 마스크 밖 비트를 실어 보냈을 때의 방어다.

### 4.4 단계 4 — 얇은 래퍼를 올린다

```c
static inline void reg_set_bits(volatile uint32_t *reg, uint32_t mask)   { reg_update(reg, mask, mask); }
static inline void reg_clear_bits(volatile uint32_t *reg, uint32_t mask) { reg_update(reg, mask, 0u);   }
static inline void reg_set_field(volatile uint32_t *reg, uint32_t mask, uint32_t val)
                                                                         { reg_update(reg, mask, FIELD_PREP(mask, val)); }
```

래퍼를 두면 드라이버 코드에서 shift와 마스크가 사라지고 의미만 남는다. `reg_set_bits`는 멱등이라 재진입해도 안전하다.

### 4.5 단계 5 — W1C는 별도 함수로 분리한다

```c
static inline void reg_w1c(volatile uint32_t *reg, uint32_t mask)
{
    *reg = mask;                          /* read 없음. 지울 비트만 1. */
}
```

이름을 따로 주는 것이 핵심이다. `reg_set_bits(STATUS, OVERRUN)`처럼 쓸 수 없게 만들면 리뷰에서 실수가 눈에 띈다. 실제 BSP에서는 레지스터 이름 자체에 규약을 넣기도 한다(`..._ICR`은 interrupt clear register라서 언제나 직접 write).

### 4.6 단계 6 — 드라이버 함수에서 순서를 지킨다
```c
reg_clear_bits(ctrl, UART_CTRL_EN);                                /* 1) disable */
reg_update(ctrl, mask, val);                                       /* 2) 필드 교체 */
reg_set_bits(ctrl, UART_CTRL_EN | UART_CTRL_TXE | UART_CTRL_RXE);  /* 3) enable  */
```

세 번의 RMW = read 3회 + write 3회다. "왜 한 번에 안 하나"를 물으면 EN을 내린 상태에서 설정이 반영되어야 한다는 IP 규칙 때문이라고 답하고, 그런 규칙이 없는 IP라면 한 번의 `reg_update`로 합치는 것이 맞다고 덧붙인다.

---

## 5. 전체 코드 읽기

`solutions/03_reg_bitfield.c`의 레이아웃 정의는 마스크 매크로만으로 표현되고, 비트의 성격은 주석으로 못박는다.

```c
#define UART_CTRL_BAUDDIV_MASK  GENMASK(19, 8)     /* 12-bit 분주비 */
#define UART_CTRL_RXTRIG_MASK   GENMASK(23, 20)    /* RX FIFO 트리거 레벨 */
#define UART_CTRL_RSVD_MASK     GENMASK(31, 24)    /* reserved: 읽은 값 보존 */
#define UART_ST_RXLVL_MASK      GENMASK(7, 4)      /* RO, RX FIFO 적재량 */
#define UART_ST_OVERRUN         BIT(8)             /* W1C */
#define UART_ST_W1C_MASK        GENMASK(11, 8)
```

`RSVD_MASK`는 테스트에서 "reserved가 보존되었는가"를 assert하려고 정의했다. 드라이버 코드가 이 마스크를 쓰는 일은 없어야 한다. 반대로 `W1C_MASK`는 드라이버가 직접 쓴다. 덕분에 호출자가 실수로 RO 비트를 섞어 넘겨도 W1C 비트만 버스로 나간다.

```c
static void uart_clear_errors(uart_sim_t *hw, uint32_t mask)
{
    sim_status_write(hw, mask & UART_ST_W1C_MASK);   /* read 없음, write 1회 */
}

static void uart_clear_errors_rmw_BUG(uart_sim_t *hw, uint32_t mask)
{
    uint32_t s = sim_status_read(hw);
    sim_status_write(hw, s | mask);                  /* 읽은 1들이 전부 clear 명령이 된다 */
}
```

host에서 W1C를 재현하려고 작은 시뮬레이터를 뒀다. 아래 함수가 곧 "버스가 하는 일"의 모델이다. CTRL은 평범한 R/W 레지스터라 시뮬레이터 없이 `volatile uint32_t` 변수의 주소를 그대로 넘긴다. 그래서 테스트는 실제 드라이버와 같은 경로를 탄다.

```c
static void sim_status_write(uart_sim_t *hw, uint32_t val)
{
    hw->writes++;
    hw->status &= ~(val & UART_ST_W1C_MASK);   /* 1을 쓴 W1C 비트만 클리어 */
    /* RO 비트와 reserved 비트에 쓴 값은 무시된다. */
}
```

ISR 서비스 루프는 이 형태가 표준이다.

```c
uint32_t pending = uart_pending_errors(hw);   /* read 1회: 이 스냅샷만 신뢰한다 */
handled = pending;                            /* 스냅샷에 있는 것만 처리 */
uart_clear_errors(hw, handled);               /* 처리한 비트만 W1C write */
```

read를 한 번만 하고, 처리한 비트만 되쓴다. 클리어 직후 하드웨어가 새 플래그를 올리면 그건 다음 인터럽트에서 본다. 그래서 유실이 없다.

---

## 6. 동시성·메모리 관점

### 6.1 volatile이 보장하는 것

```c
while ((*UART0_STATUS & UART_ST_TXRDY) == 0u) { }
```

`volatile`이 없으면 컴파일러는 "이 메모리는 루프 안에서 아무도 안 바꾼다"고 판단해 한 번 읽고 무한 루프로 최적화한다. `volatile`은 (1) 접근을 지우지 않고 (2) 다른 volatile 접근과의 상대 순서를 바꾸지 않는다를 보장한다.
보장하지 않는 것도 분명하다(이 구분을 못 하면 곧바로 꼬리질문에 걸린다). `volatile`은 원자성을 주지 않고, CPU의 store buffer나 버스 브리지의 지연을 막지 못하며, 다른 코어에서의 가시성도 보장하지 않는다. RMW 중간에 인터럽트가 끼어드는 것도 못 막는다.

### 6.2 ISR과 task가 같은 CTRL을 RMW 할 때

```text
 task                                 ISR                       CTRL 값
 ----                                 ---                       -------
 r = *CTRL            (0xA53111C7)                              0xA53111C7
 r = (r & ~BAUD)|new  (레지스터 안)
       -- 인터럽트! --------------->  reg_set_bits(CTRL, LB)    0xA53111CF
                                      (복귀)
 *CTRL = r            (0xA53222C7)                              0xA53222C7
                                                                        ^
                                              ISR이 켠 LOOPBACK(bit3)이 사라졌다
```

task가 읽은 낡은 스냅샷을 되쓰면서 그 사이의 변경을 덮어 버린다. 해결책은 세 가지다.

1. RMW 구간만 짧게 인터럽트를 막는다(critical section). 가장 흔하고, 몇 사이클이면 끝난다.
2. IP가 제공하는 **SET / CLR 별도 레지스터**를 쓴다. 많은 GPIO와 인터럽트 컨트롤러가 `BSRR`, `ISER`, `ICER` 같은 write-only 레지스터를 준다. 1을 쓴 비트만 동작하므로 read가 필요 없고, 따라서 경쟁 자체가 없다.
3. 소유권을 나눈다. "이 레지스터는 task만 만진다"로 설계하고 ISR은 플래그만 남긴다.

Cortex-M3/M4의 bit-banding도 답이 되지만(단일 비트를 별도 주소에 매핑해 하드웨어가 RMW를 원자적으로 수행), 최신 코어에는 없는 경우가 많으니 "있으면 쓴다" 정도로 말한다.

### 6.3 쓰기가 늦게 도착하는 문제

```text
  STR (플래그 클리어)  --> [write buffer] --> [APB bridge] --> peripheral
                                     지연 수 사이클
  BX LR (ISR 종료)  ----> NVIC가 아직 내려가지 않은 라인을 보고 재진입!
```

인터럽트 플래그를 지운 직후 ISR을 빠져나오면 같은 인터럽트가 다시 걸릴 수 있다. 관용구는 "클리어한 레지스터를 한 번 다시 읽기"(read-back)나 `__DSB()`다. read-back은 버스 트랜잭션이 완료될 때까지 CPU를 세우는 효과를 낸다.

### 6.4 왜 `struct` bit-field로 레지스터를 매핑하지 않나

```c
/* 권하지 않는 방식 */
typedef struct {
    volatile uint32_t EN     : 1;
    volatile uint32_t TXE    : 1;
    volatile uint32_t RXE    : 1;
    volatile uint32_t LB     : 1;
    volatile uint32_t PARITY : 2;
    /* ... */
} uart_ctrl_t;
```

C 표준이 구현에 맡긴 것이 너무 많다.

```text
비트 할당 순서가 구현 정의:
  little-endian ABI      MSB                             LSB
                         [ ... | PARITY | LB | RX | TX | EN ]   EN이 bit0
  다른 ABI/컴파일러       [ EN | TX | RX | LB | PARITY | ... ]   EN이 bit31

접근 폭도 구현 정의:
  기대:  LDR r0,[r1] ; BIC ; ORR ; STR r0,[r1]      (32-bit 1회)
  실제:  LDRB r0,[r1] ; ORR ; STRB r0,[r1]          (8-bit 접근!)
         -> 32비트 접근만 허용하는 주변장치에서 bus fault
         -> 인접 필드를 건드려 W1C 비트를 날릴 수도 있다
```

정리하면 (1) 비트 할당 순서, (2) 필드가 저장 단위를 넘을 때의 packing, (3) 컴파일러가 만드는 접근 폭과 횟수가 모두 구현 정의다. 게다가 `s.PARITY = 2;` 한 줄이 read-modify-write인지 아닌지 소스에서 보이지 않는다. 그래서 CMSIS를 포함한 벤더 헤더는 레지스터 **워드**는 `volatile uint32_t`로 두고, 비트는 `_Pos` / `_Msk` 매크로로 표현한다. 이 문제의 해답도 정확히 그 방식이다.

구조체 자체가 나쁜 것은 아니다. 레지스터 블록을 `volatile uint32_t` 멤버들의 struct로 묶는 것(`CTRL`, `STATUS`, `DATA`)은 표준 관행이고 오프셋을 `_Static_assert`로 검증하면 안전하다. 문제는 워드 안을 비트필드로 쪼개는 것이다.
---

## 7. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `1 << 31`로 마스크 생성 | UBSan이 잡거나 최적화 레벨에 따라 값이 달라짐 | signed int overflow는 UB | 항상 `(uint32_t)1u << n` |
| `1UL << n` | 타깃은 되는데 host 테스트만 실패 | `long`이 host에서 64-bit | `uint32_t`로 폭 고정 |
| 다중 비트 필드에 `\|=` | 옛 값과 새 값이 OR로 섞임 | OR는 0으로 못 내림 | `(r & ~mask) \| (val & mask)` |
| `*reg = FIELD_PREP(...)` 통째 쓰기 | 다른 기능이 꺼지고 reserved가 0이 됨 | 나머지 비트를 안 보존 | `reg_update`로 RMW |
| W1C STATUS를 RMW | 에러 카운터가 0인데 데이터가 깨짐, 인터럽트 씹힘 | 읽은 1들이 전부 clear 명령이 됨 | `reg_w1c`로 지울 비트만 write |
| `volatile` 누락 | 폴링 루프가 무한 루프, 디버그 빌드에서만 동작 | 컴파일러가 재읽기를 제거 | 레지스터 포인터에 `volatile` |
| ISR과 task가 같은 레지스터 RMW | 설정이 가끔 되돌아감 | read와 write 사이 선점 | critical section 또는 SET/CLR 레지스터 |
| 플래그 클리어 직후 ISR 종료 | 같은 인터럽트 즉시 재진입 | write가 아직 도착 안 함 | read-back 또는 `__DSB()` |
| `struct` bit-field로 레지스터 매핑 | 다른 컴파일러/포팅에서 깨짐, bus fault | 비트 순서와 접근 폭이 구현 정의 | `_Pos` / `_Msk` 매크로 방식 |

---

## 8. 직접 확인하기

```bash
make sol N=03     # 모범답안 실행: ok 줄 11개 + ALL TESTS PASSED
make run N=03     # 내 구현 실행
```

`make sol N=03`에서 특히 볼 줄은 이 셋이다.

```text
  ok  reg_update: 다중 비트 필드만 바뀌고 이웃/reserved는 보존
  ok  W1C 올바른 클리어: 지울 비트만 write, 다른 flag 보존
  ok  W1C에 RMW를 쓰면 처리하지 않은 flag까지 클리어된다 (버그 재현)
```

일부러 깨뜨려 보는 실험 세 가지.
1. `FIELD_PREP`에서 `& (mask)`를 지우고 `make sol N=03`을 돌린다. `test_prep_get`의 truncation assert가 깨지고, 4비트 필드에 `0x1F`를 넣으면 bit24(reserved)가 오염되는 것이 보인다.
2. `uart_clear_errors`를 `uart_clear_errors_rmw_BUG`와 같은 구현으로 바꾼다. `test_w1c_correct`가 "FRAMEERR가 살아있어야 한다"에서 실패한다. 이것이 필드에서는 "인터럽트가 가끔 씹힌다"로 보이는 바로 그 현상이다.
3. `reg_update`를 `*reg = (*reg & ~mask) | (val & mask);` 한 줄로 바꾼다. 테스트는 통과한다. 통과한다는 사실 자체가 교훈이다. 테스트로는 volatile 접근 횟수를 못 잡으니 컴파일러 출력으로 확인해야 한다.

어셈블리는 `cc -std=c11 -O2 -S solutions/03_reg_bitfield.c -o -` 로 본다. 이를 보면 상수 마스크가 접혀 immediate로 들어간 것을 볼 수 있다. Cortex-M 타깃이라면 `uart_set_baud` 하나가 `LDR` / `BIC` / `ORR` / `STR` 네 명령이다.

---

## 9. 면접에서 말하기

설명 순서는 이렇게 잡는다. 먼저 "레지스터 접근 단위는 워드다"를 말하고, 그래서 필드 갱신이 read-modify-write가 된다고 잇는다. 다음으로 마스크·shift 매크로를 화이트보드에 그리고, `reg_update` 세 줄을 쓴다. 마지막에 **먼저 묻지 않아도** W1C와 ISR 경쟁을 스스로 꺼낸다. 이 마지막 한 문단이 "드라이버를 실제로 짜 본 사람"과 "비트 연산을 아는 사람"을 가른다. 그대로 쓸 영어 문장 다섯 개는 이렇다.

1. "Register access is word-granular, so changing a field is a read, a mask, an or, and a single write. I keep that in one `reg_update` helper so the number of bus transactions is obvious."
2. "I build masks with `GENMASK(hi, lo)` and derive the shift from the mask with a count-trailing-zeros builtin, so there is a single source of truth per field instead of a separate position constant that can drift."
3. "Everything is fixed to `uint32_t`. `1 << 31` is signed overflow, and `1UL` silently becomes 64-bit when we run the host unit tests."
4. "On a write-one-to-clear status register, read-modify-write is not a style issue, it is a lost-interrupt bug: every flag that happened to read as one gets written back as one, which clears flags we never handled."
5. "If an ISR and a task both touch the same control register, I either guard the read-modify-write with a short critical section or use the IP's separate set and clear registers, which need no read at all."

화이트보드에 그리는 순서.

1. 32비트 칸에 비트 번호를 쓰고 필드 경계를 긋는다. 이어서 `GENMASK`를 두 번의 shift로 유도한다.
2. `before` / `~mask` / `cleared` / `after` 네 줄을 비트로 그린다.
3. STATUS를 새로 그리고 W1C 비트를 표시한 뒤, 잘못된 `|=`의 write 값과 결과를 한 줄씩 그린다.
4. task와 ISR 두 줄짜리 타임라인으로 RMW 경쟁을 그린다.

---

## 10. follow-up 답안

**"Show me what this compiles to. How many bus transactions per `uart_set_baud`?"**
상수 마스크는 전부 컴파일 타임에 접히므로 Cortex-M에서 `LDR` 한 번, `BIC`/`ORR` 레지스터 연산, `STR` 한 번이다. 버스 트랜잭션은 read 1회 + write 1회로 딱 두 번이다. `reg_update`가 인라인되므로 함수 호출 오버헤드도 없다. `uart_config`는 RMW를 세 번 하므로 read 3회 + write 3회이고, disable 요구가 없는 IP라면 한 번으로 줄일 수 있다.

**"Two writers, an ISR and a task, both touch CTRL. Fix it without disabling interrupts for long."**
우선순위는 (1) IP가 SET/CLR 레지스터를 주면 그걸 쓴다. read가 없으니 경쟁 자체가 사라진다. (2) 없으면 RMW 세 줄만 감싸는 critical section을 쓴다. Cortex-M이면 `PRIMASK`를 저장·복원하는 방식이라 수 사이클이고, `BASEPRI`를 쓰면 고우선순위 인터럽트는 계속 받을 수 있다. (3) 구조적으로는 레지스터의 소유자를 한 문맥으로 정하고 반대쪽은 큐로 요청을 넘긴다. 락을 잡은 채로 폴링 루프를 도는 일만은 피한다.

**"The vendor header maps this register as a `struct` with bit-fields. Would you use it?"**
읽기 전용 디버그 용도라면 편하지만 드라이버 본체에는 쓰지 않는다. 비트 할당 순서, 저장 단위 경계에서의 packing, 컴파일러가 선택하는 접근 폭과 횟수가 모두 구현 정의라서다. 실제로 바이트 접근으로 컴파일되어 32비트 접근만 허용하는 주변장치에서 fault가 나거나, 인접한 W1C 비트를 건드리는 사고가 보고된다. 워드 단위 `volatile uint32_t`와 `_Msk` 매크로가 생성 코드가 예측 가능하고 이식성도 좋다.

**"`volatile` versus `_Atomic` versus a memory barrier. Which do you actually need?"**
MMIO 레지스터에는 `volatile`이 필요하다. 접근이 제거되지 않고 다른 volatile 접근과 순서가 유지된다. `_Atomic`은 여러 실행 문맥이 공유하는 **메모리 변수**(예: ISR과 task가 나눠 쓰는 링버퍼 인덱스)에 필요한 것이지, 레지스터의 RMW를 원자적으로 만들어 주지는 않는다. 배리어는 다른 문제를 푼다. 쓰기가 주변장치에 도착하는 시점을 맞춰야 할 때(`__DSB()` 후 sleep 진입, 인터럽트 클리어 후 ISR 종료)와 DMA 버퍼와 디스크립터 쓰기 순서를 맞출 때 쓴다. 정리하면 volatile은 컴파일러를, 배리어는 CPU/버스를, atomic은 공유 변수를 다룬다.

**"How would you unit-test a driver like this on the host, with no hardware?"**
레지스터 블록을 구조체로 정의하고 드라이버가 베이스 포인터를 인자로 받게 만든다. 그러면 타깃에서는 `0x40004000`을, host 테스트에서는 일반 메모리의 구조체 주소를 넘길 수 있다. 이 문제의 답안이 정확히 그 구조다. W1C나 read-to-clear처럼 쓰기가 값을 바꾸는 레지스터는 평범한 메모리로 흉내 낼 수 없으므로 접근 함수를 통해 버스 동작을 모델링한다. 여기서는 `sim_status_write`가 그 역할을 하고, 읽기/쓰기 횟수까지 세어 "read 없이 write 1회"를 assert로 검증한다.

---

## 11. 요약 & 체크리스트

마스크는 shift 두 번으로 만들고 폭은 `uint32_t`로 못박는다. 필드 갱신은 `(r & ~mask) | (val & mask)`를 지역 변수에서 계산해 read 1회 write 1회로 끝낸다. W1C와 공유 레지스터에서는 RMW 자체가 버그다.
- [ ] `BIT`, `GENMASK`, `FIELD_MASK`, `FIELD_PREP`, `FIELD_GET`을 보지 않고 쓸 수 있다
- [ ] `hi == 31`, `width == 32`에서 UB가 없는 이유와 `1 << 31` / `1UL << n`의 위험을 설명할 수 있다
- [ ] `reg_update`가 read 1회 write 1회임을 코드로 보이고, 그게 왜 중요한지 말할 수 있다
- [ ] 다중 비트 필드에 `|=`를 쓰면 값이 어떻게 섞이는지, read-only/reserved 비트는 어떻게 취급하는지 말할 수 있다
- [ ] W1C 레지스터에서 `|=`가 인터럽트를 잃는 과정을 write 값까지 그려서 설명할 수 있다
- [ ] ISR과 task의 RMW 경쟁 타임라인을 그리고 해결책 세 가지를 댈 수 있다
- [ ] `volatile`이 보장하는 것과 보장하지 않는 것을 구분할 수 있다
- [ ] `struct` bit-field가 레지스터 매핑에 부적합한 이유를 세 가지 댈 수 있다
- [ ] 인터럽트 플래그 클리어 후 read-back이나 `__DSB()`가 필요한 이유를 말할 수 있다
