/* C-M03 데모: 같은 비트필드 레지스터가 타깃(ABI)마다 어떤 바이트가 되는가 — 컴파일만 한다 (main 없음).
 * <stdint.h> 만 include → 프리스탠딩 타깃에서도 컴파일된다.
 *
 *   clang -std=c11 -O2 -S demo_layout.c -o -                                                  # 호스트 arm64 macOS
 *   clang -std=c11 -O2 -S --target=thumbv7em-none-eabi   -mcpu=cortex-m4 demo_layout.c -o -    # Cortex-M4 LE
 *   clang -std=c11 -O2 -S --target=thumbebv7em-none-eabi -mcpu=cortex-m4 demo_layout.c -o -    # Cortex-M4 BE
 *   clang -std=c11 -O2 -S --target=x86_64-pc-windows-msvc demo_layout.c -o -                   # MSVC ABI
 *   # 크로스 오브젝트로 확인: -c 로 .o 를 만들고 xcrun llvm-objdump -s -j .rodata x.o
 *
 * 비트필드의 비트 위치는 정수 상수식으로 꺼낼 수 없어서 _Static_assert 로 못 박을 수 없다
 * (union 다른 멤버 읽기·__builtin_bit_cast 모두 상수식 아님 — 풀이 참고). 그래서
 *   (1) sizeof / _Alignof 처럼 상수식인 것은 _Static_assert 로 타깃별 기대값을 고정하고
 *   (2) 비트 위치는 const 객체의 초기값 바이트(.byte 줄)를 asm/objdump 로 읽어 비교한다.
 */
#include <stdint.h>

typedef struct {
    uint32_t   en     : 1;
    uint32_t   mode   : 2;
    uint32_t   res    : 2;
    uint32_t   smpt   : 8;
    signed int offset : 7;
    uint32_t          : 4;
    uint32_t   ch     : 5;
    uint32_t   start  : 1;
    uint32_t          : 2;
} adc_cfg_bits_t;

/* 필드 하나만 전부 1 → 그 필드가 차지하는 비트가 바이트로 보인다 */
const adc_cfg_bits_t k_en   = { .en = 1 };
const adc_cfg_bits_t k_mode = { .mode = 3 };
const adc_cfg_bits_t k_smpt = { .smpt = 0xFF };
const adc_cfg_bits_t k_off  = { .offset = -1 };
const adc_cfg_bits_t k_ch   = { .ch = 0x1F };
/* 마스크 버전으로 계산하면 0x070FAB4D 가 되어야 하는 설정 */
const adc_cfg_bits_t k_mix  = { .en = 1, .mode = 2, .res = 1, .smpt = 0x5A, .offset = -3, .ch = 7 };

/* ---- 저장 단위 / straddle / 선언 타입 규칙: 크기는 상수식이라 타깃별로 고정할 수 있다 ---- */
typedef struct { unsigned a : 30; unsigned b : 4; } s30_4;    /* 32비트 경계를 넘을까? */
typedef struct { uint8_t  a : 6;  uint8_t  b : 4; } u8_6_4;    /* uint8_t 비트필드 = 구현 정의 타입 */
typedef struct { uint8_t  a : 4;  uint32_t b : 4; } mix_8_32;  /* 선언 타입이 섞이면? */
typedef struct { uint8_t  a : 4;  uint16_t b : 4; uint8_t c : 4; } mix_8_16_8;

#if defined(_MSC_VER)
/* MSVC 규칙: 선언 타입의 크기가 바뀌면 새 저장 단위를 연다 */
_Static_assert(sizeof(mix_8_32) == 8,   "MSVC: uint8_t unit + new uint32_t unit");
_Static_assert(sizeof(mix_8_16_8) == 6, "MSVC: 3 separate units");
#else
/* SysV / AAPCS / Apple arm64: 선언 타입 크기의 정렬된 구간을 넘지 않는 한 이어서 채운다 */
_Static_assert(sizeof(mix_8_32) == 4,   "SysV/AAPCS: b shares the 32-bit container");
_Static_assert(sizeof(mix_8_16_8) == 2, "SysV/AAPCS: all 12 bits packed");
#endif
_Static_assert(sizeof(adc_cfg_bits_t) == 4, "register struct is 32 bits");
_Static_assert(sizeof(s30_4) == 8,  "b does not straddle: moves to the next unsigned unit");
_Static_assert(sizeof(u8_6_4) == 2, "b does not straddle a byte");

const unsigned sz_mix_8_32 = sizeof(mix_8_32), sz_mix_8_16_8 = sizeof(mix_8_16_8);

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
const char k_order_note[] = "big-endian: bitfields allocated MSB-first";
#else
const char k_order_note[] = "little-endian: bitfields allocated LSB-first";
#endif
