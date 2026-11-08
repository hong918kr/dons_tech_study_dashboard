/* C-M03 데모: 비트필드 쓰기가 만드는 어셈블리 (RMW, 접근 폭) — 컴파일만 한다 (main 없음).
 *
 *   clang -std=c11 -O2 -S --target=thumbv7em-none-eabi -mcpu=cortex-m4 demo_rmw.c -o -     # Cortex-M4
 *   clang -std=c11 -O2 -S --target=thumbv7em-none-eabi -mcpu=cortex-m4 \
 *         -fno-aapcs-bitfield-width demo_rmw.c -o -                                         # AAPCS 폭 규칙 끔
 *   clang -std=c11 -O2 -S demo_rmw.c -o -                                                   # 호스트 arm64
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

/* 벤더 헤더 흉내: 비트필드 뷰(.b)와 워드 뷰(.w)를 union 으로 (TI C2000 의 .bit/.all, CMSIS APSR_Type 의 b/w) */
typedef union { adc_cfg_bits_t b; uint32_t w; } adc_cfg_reg_t;
typedef struct { volatile uint32_t SR; volatile adc_cfg_reg_t CFG; volatile uint32_t DR; } ADC_BF_TypeDef;
typedef struct { volatile uint32_t SR; volatile uint32_t CFG; volatile uint32_t DR; } ADC_TypeDef;

#define ADC_CFG_CH_Pos    24u
#define ADC_CFG_CH_Msk    (0x1Fu << ADC_CFG_CH_Pos)
#define ADC_CFG_START_Msk (0x1u << 29)

/* ① volatile 비트필드에 필드 하나 쓰기 → ldr / bfi / str (RMW 1회) */
void bf_set_ch(ADC_BF_TypeDef *a, unsigned ch) { a->CFG.b.ch = ch; }

/* ② 필드 두 개 → RMW 두 번. volatile 이라 합칠 수 없다 */
void bf_start_ch(ADC_BF_TypeDef *a, unsigned ch) { a->CFG.b.ch = ch; a->CFG.b.start = 1; }

/* ③ 같은 코드인데 volatile 이 아닌 RAM 구조체 → 컴파일러가 하나로 합친다 */
void ram_start_ch(adc_cfg_reg_t *r, unsigned ch) { r->b.ch = ch; r->b.start = 1; }

/* ④ 마스크 버전: 읽기 1 + 쓰기 1 을 소스가 직접 정한다 */
void mask_start_ch(ADC_TypeDef *a, unsigned ch) {
    uint32_t v = a->CFG;
    v = (v & ~ADC_CFG_CH_Msk) | ((ch << ADC_CFG_CH_Pos) & ADC_CFG_CH_Msk) | ADC_CFG_START_Msk;
    a->CFG = v;
}

/* ⑤ 로컬 사본(shadow) 패턴: 비트필드는 레지스터가 아닌 지역 변수에만 → 읽기 1 + 쓰기 1 */
void shadow_start_ch(ADC_BF_TypeDef *a, unsigned ch) {
    adc_cfg_reg_t t;
    t.w = a->CFG.w;
    t.b.ch = ch; t.b.start = 1;
    a->CFG.w = t.w;
}

/* ⑥ signed int 비트필드 읽기 → sbfx (부호 확장 추출) */
int bf_get_offset(ADC_BF_TypeDef *a) { return a->CFG.b.offset; }

/* ⑦ 접근 폭: 4+4 비트만 쓰는 volatile uint32_t 비트필드 레지스터 (sizeof == 4) */
typedef struct { volatile uint32_t lo : 4; volatile uint32_t hi : 4; } nib_reg_t;
void nib_set_hi(nib_reg_t *r) { r->hi = 3; }

/* ⑧ 선언 타입이 uint8_t 인 비트필드 레지스터 (일부 8비트 MCU 헤더 스타일) */
typedef struct { volatile uint8_t en : 1; volatile uint8_t mode : 2; uint8_t : 5; uint8_t pad[3]; } u8_reg_t;
void u8_set_en(u8_reg_t *r) { r->en = 1; }
