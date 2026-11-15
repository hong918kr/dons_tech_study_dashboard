/* BM-B01 벤더 헤더 읽기: CMSIS 디바이스 헤더와 _Pos/_Msk 관례
 *
 * Part A — 헤더 읽기: 베이스 주소 체인 · 구조체 오프셋 · _Msk 값을 손으로 계산 (ANS_* 상수)
 *          + RM0090 오프셋 표를 _Static_assert(offsetof) 로 고정 (어긋나면 컴파일이 안 된다)
 * Part B — CMSIS 필드 매크로 VAL2FLD / FLD2VAL / MODIFY_REG 직접 쓰기 + 고전 버그 갤러리
 * Part C — 매크로만으로 쓴 설정 함수: PA5 LED, USART2 핀(PA2/PA3 AF7), USART2 8N1
 *          테스트 빌드(UNIT_TEST)는 GPIOA/RCC/USART2 를 RAM 구조체로 바꿔 레지스터 값을 검사한다.
 *
 * 빌드 (레슨 폴더에서 — device_excerpt.h 를 include 한다):
 *   cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
 * 타깃 빌드: UNIT_TEST 없이 컴파일하면 GPIOA 는 헤더 그대로 ((GPIO_TypeDef *)0x40020000UL) — demo_target.c
 * 주소·오프셋·비트는 RM0090 기준 대표값 (확인 권장). 테스트의 정답은 device_excerpt.h 의 정의다.
 */
#define UNIT_TEST 1

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "device_excerpt.h"

/* ================= Part A-1: RM0090 오프셋 표 ↔ 헤더 구조체 (컴파일 시점 검사) ================= */
#define RM_OFF(T, f, off) _Static_assert(offsetof(T, f) == (off), #T "." #f " is not at RM offset " #off)
RM_OFF(GPIO_TypeDef, MODER, 0x00);   RM_OFF(GPIO_TypeDef, OTYPER, 0x04);
RM_OFF(GPIO_TypeDef, OSPEEDR, 0x08); RM_OFF(GPIO_TypeDef, PUPDR, 0x0C);
RM_OFF(GPIO_TypeDef, IDR, 0x10);     RM_OFF(GPIO_TypeDef, ODR, 0x14);
RM_OFF(GPIO_TypeDef, BSRR, 0x18);    RM_OFF(GPIO_TypeDef, LCKR, 0x1C);
RM_OFF(GPIO_TypeDef, AFR[0], 0x20);  RM_OFF(GPIO_TypeDef, AFR[1], 0x24);
RM_OFF(USART_TypeDef, SR, 0x00);     RM_OFF(USART_TypeDef, DR, 0x04);
RM_OFF(USART_TypeDef, BRR, 0x08);    RM_OFF(USART_TypeDef, CR1, 0x0C);
RM_OFF(USART_TypeDef, CR2, 0x10);    RM_OFF(USART_TypeDef, CR3, 0x14);
RM_OFF(RCC_TypeDef, AHB1RSTR, 0x10); RM_OFF(RCC_TypeDef, APB1RSTR, 0x20);
RM_OFF(RCC_TypeDef, APB2RSTR, 0x24); RM_OFF(RCC_TypeDef, AHB1ENR, 0x30);
RM_OFF(RCC_TypeDef, APB1ENR, 0x40);  RM_OFF(RCC_TypeDef, APB2ENR, 0x44);
_Static_assert(sizeof(GPIO_TypeDef) == 0x28, "GPIO_TypeDef size");
_Static_assert(sizeof(GPIO_TypeDef) <= GPIOB_BASE - GPIOA_BASE, "GPIO block must fit its 0x400 slot");

/* ================= Part A-2: 헤더 읽기 퀴즈 — device_excerpt.h 만 보고 손으로 계산 ================= */
#define ANS_GPIOA_ODR_ADDR    0x00000000u   /* TODO: &GPIOA->ODR 의 주소 */
#define ANS_GPIOC_BSRR_ADDR   0x00000000u   /* TODO: &GPIOC->BSRR */
#define ANS_USART2_BRR_ADDR   0x00000000u   /* TODO: &USART2->BRR */
#define ANS_RCC_APB1ENR_ADDR  0x00000000u   /* TODO: &RCC->APB1ENR */
#define ANS_MODER5_MSK        0x00000000u   /* TODO: GPIO_MODER_MODER5_Msk */
#define ANS_AFSEL3_MSK        0x00000000u   /* TODO: GPIO_AFRL_AFSEL3_Msk */
#define ANS_CR1_UE_MSK        0x00000000u   /* TODO: USART_CR1_UE_Msk */
#define ANS_GPIO_SIZE         0u            /* TODO: sizeof(GPIO_TypeDef) (바이트) */

/* ================= Part B: CMSIS 필드 매크로 =================
 * CMSIS core_cm4.h 의 _VAL2FLD / _FLD2VAL, stm32f4xx.h 의 MODIFY_REG 와 같은 의미로 쓴다.
 * (밑줄+대문자로 시작하는 이름은 C 예약 식별자라 내 코드에서는 밑줄 없이)
 *   VAL2FLD(F, v)  : 값 v 를 필드 F 자리로 올리고 F 밖으로 넘친 비트는 잘라낸다
 *   FLD2VAL(F, r)  : 레지스터 값 r 에서 필드 F 를 꺼내 0 번 비트로 내린다
 *   MODIFY_REG(R, C, S) : R = (R & ~C) | S  — 읽기 1번, 쓰기 1번
 */
#define VAL2FLD(field, value)   ((void)(value), 0UL)                                   /* TODO */
#define FLD2VAL(field, value)   ((void)(value), 0UL)                                   /* TODO */
#define MODIFY_REG(REG, CLEARMASK, SETMASK) ((void)(REG), (void)(CLEARMASK), (void)(SETMASK)) /* TODO */

/* ---- 테스트 빌드: 헤더의 캐스트 매크로를 RAM 구조체로 바꿔치기 ---- */
#ifdef UNIT_TEST
static GPIO_TypeDef  ram_gpioa;
static USART_TypeDef ram_usart2;
static RCC_TypeDef   ram_rcc;
#undef GPIOA
#undef USART2
#undef RCC
#define GPIOA  (&ram_gpioa)
#define USART2 (&ram_usart2)
#define RCC    (&ram_rcc)
#endif

/* ---- 버그 갤러리 (주어진 코드 — 고치지 말고 테스트가 보여 주는 결과를 읽어라) ---- */
static void bug_pos_as_msk_moder(void) { GPIOA->MODER |= GPIO_MODER_MODER5_Pos; }      /* 10 = 0b1010 */
static void bug_pos_as_msk_cr1(void)   { SET_BIT(USART2->CR1, USART_CR1_UE_Pos); }     /* 13 = 0b1101 */
static void bug_no_clear(void)         { GPIOA->MODER |= GPIO_MODER_MODER5_0; }        /* 전에 AF(10) 였다면? */
static void bug_alias_as_value(void) {                                                 /* MODER5 = _Msk 별칭 */
    GPIOA->MODER = (GPIOA->MODER & ~GPIO_MODER_MODER5_Msk) | GPIO_MODER_MODER5;
}
static void bug_overwrite(void)        { GPIOA->MODER = GPIO_MODER_MODER5_0; }         /* RMW 대신 덮어쓰기 */

/* ================= Part C: 매크로만으로 쓴 설정 함수 ================= */
enum { MODE_IN = 0u, MODE_OUT = 1u, MODE_AF = 2u, MODE_AN = 3u };
enum { SPEED_LOW = 0u, SPEED_MED = 1u, SPEED_FAST = 2u, SPEED_HIGH = 3u };
enum { PULL_NONE = 0u, PULL_UP = 1u, PULL_DOWN = 2u };
#define AF7_USART2 7u

/* PA5 = LED: GPIOA 클럭 켜기 → 출력, push-pull, low speed, 풀 없음. 다른 핀·다른 클럭 비트는 그대로. */
static void led_pa5_init(void) {
    /* TODO: RCC->AHB1ENR, GPIOA->MODER / OTYPER / OSPEEDR / PUPDR — 매크로만 써서 */
}

/* LED 켜기/끄기: ODR RMW 대신 BSRR 한 번 쓰기 (BM-B04 참고) */
static void led_pa5_set(bool on) {
    (void)on; /* TODO */
}

/* PA2 = USART2_TX: AF7, push-pull, medium speed, 풀 없음 / PA3 = USART2_RX: AF7, 풀업 */
static void usart2_pins_init(void) {
    /* TODO: GPIOA 클럭, MODER(2·3), OTYPER(2), OSPEEDR(2), PUPDR(2·3), AFR[0](2·3) */
}

/* USART2 8N1, OVER8=0, TE+RE, 마지막에 UE. BRR = round(pclk1 / baud) (= mantissa<<4 | fraction).
 * 0 성공 / -1 잘못된 인자 (baud 0, BRR < 16 이거나 > 0xFFFF) — 실패하면 레지스터를 건드리지 않는다. */
static int usart2_init(uint32_t pclk1_hz, uint32_t baud) {
    (void)pclk1_hz; (void)baud;
    return -1; /* TODO */
}

/* ================= 테스트 하네스 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng = 0x2545F491u;                       /* xorshift32, 고정 시드 */
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* RM0090 리셋값 (대표값 — 확인 권장): GPIOA 는 PA13/PA14/PA15 가 디버그(SWD/JTAG) AF 로 시작한다 */
static void fixture_reset(void) {
    ram_gpioa  = (GPIO_TypeDef){ .MODER = 0xA8000000u, .OSPEEDR = 0x0C000000u, .PUPDR = 0x64000000u };
    ram_usart2 = (USART_TypeDef){ .SR = 0x000000C0u };
    ram_rcc    = (RCC_TypeDef){ .AHB1ENR = 0x00100000u };  /* CCMDATARAMEN(bit20) 만 1 */
}

/* 임의의 (pos, width) 필드를 매크로에 넣기: TFLD ## _Pos → TFLD_Pos → t_pos (런타임 변수) */
static uint32_t t_pos, t_msk;
#define TFLD_Pos t_pos
#define TFLD_Msk t_msk

int main(void) {
    /* ---- Part A: 퀴즈 ---- */
    CHECK(ANS_GPIOA_ODR_ADDR   == GPIOA_BASE + offsetof(GPIO_TypeDef, ODR));
    CHECK(ANS_GPIOC_BSRR_ADDR  == GPIOC_BASE + offsetof(GPIO_TypeDef, BSRR));
    CHECK(ANS_USART2_BRR_ADDR  == USART2_BASE + offsetof(USART_TypeDef, BRR));
    CHECK(ANS_RCC_APB1ENR_ADDR == RCC_BASE + offsetof(RCC_TypeDef, APB1ENR));
    CHECK(ANS_MODER5_MSK == GPIO_MODER_MODER5_Msk);
    CHECK(ANS_AFSEL3_MSK == GPIO_AFRL_AFSEL3_Msk);
    CHECK(ANS_CR1_UE_MSK == USART_CR1_UE_Msk);
    CHECK(ANS_GPIO_SIZE == sizeof(GPIO_TypeDef));

    /* ---- Part B: 매크로 ---- */
    CHECK(VAL2FLD(GPIO_MODER_MODER5, MODE_OUT) == 0x00000400u);
    CHECK(VAL2FLD(GPIO_MODER_MODER5, 7u) == 0x00000C00u);              /* 넘친 비트는 잘린다 */
    CHECK(VAL2FLD(USART_BRR_DIV_Mantissa, 0x16u) == 0x00000160u);
    CHECK(FLD2VAL(USART_BRR_DIV_Mantissa, 0x16Du) == 0x16u);
    CHECK(FLD2VAL(USART_BRR_DIV_Fraction, 0x16Du) == 0xDu);
    CHECK(FLD2VAL(GPIO_MODER_MODER5, 0xA8000C00u) == MODE_AN);
    {
        volatile uint32_t r = 0xA8000800u;                             /* PA5 = AF, PA13–15 디버그 */
        MODIFY_REG(r, GPIO_MODER_MODER5_Msk, VAL2FLD(GPIO_MODER_MODER5, MODE_OUT));
        CHECK(r == 0xA8000400u);
    }
    {   /* 무작위 (pos, width, value, reg) 2000 개: 정의대로 동작하나 + 필드 밖은 그대로인가 */
        int bad_v2f = 0, bad_roundtrip = 0, bad_modify = 0;
        for (int i = 0; i < 2000; i++) {
            uint32_t w = 1u + xs32() % 32u, pos = xs32() % (33u - w);
            uint32_t ones = (w == 32u) ? 0xFFFFFFFFu : ((1u << w) - 1u);
            t_pos = pos; t_msk = ones << pos;
            uint32_t v = xs32(), reg0 = xs32();
            if ((uint32_t)VAL2FLD(TFLD, v) != ((v << pos) & t_msk)) bad_v2f++;
            if ((uint32_t)FLD2VAL(TFLD, VAL2FLD(TFLD, v)) != (v & ones)) bad_roundtrip++;
            volatile uint32_t r = reg0;
            MODIFY_REG(r, TFLD_Msk, VAL2FLD(TFLD, v));
            if (((r ^ reg0) & ~t_msk) != 0u || (r & t_msk) != ((v << pos) & t_msk)) bad_modify++;
        }
        CHECK(bad_v2f == 0);
        CHECK(bad_roundtrip == 0);
        CHECK(bad_modify == 0);
    }
    /* 버그 갤러리: 틀린 결과를 "그대로" 확인 (시작 코드에서도 통과) */
    fixture_reset(); bug_pos_as_msk_moder();
    CHECK(ram_gpioa.MODER == 0xA800000Au);                  /* PA0, PA1 이 AF 로! PA5 는 그대로 입력 */
    fixture_reset(); bug_pos_as_msk_cr1();
    CHECK(ram_usart2.CR1 == 0x0000000Du);                   /* SBK|RE|TE — UE 는 꺼진 채 break 요청 */
    fixture_reset(); ram_gpioa.MODER |= 0x00000800u; bug_no_clear();
    CHECK(((ram_gpioa.MODER >> 10) & 3u) == MODE_AN);       /* AF(10) | 01 = 11 아날로그 */
    fixture_reset(); bug_alias_as_value();
    CHECK(((ram_gpioa.MODER >> 10) & 3u) == MODE_AN);       /* 별칭 = _Msk = 11 */
    fixture_reset(); bug_overwrite();
    CHECK(ram_gpioa.MODER == 0x00000400u);                  /* PA13/PA14 SWD 가 입력으로 → 디버거 끊김 */

    /* ---- Part C: 리셋값에서 시작 ---- */
    fixture_reset(); led_pa5_init();
    CHECK(ram_rcc.AHB1ENR == 0x00100001u);                  /* GPIOAEN 켜고 bit20 은 그대로 */
    CHECK(ram_gpioa.MODER == 0xA8000400u);                  /* PA5 출력, SWD 핀 그대로 */
    CHECK(ram_gpioa.OTYPER == 0x00000000u);
    CHECK(ram_gpioa.OSPEEDR == 0x0C000000u);
    CHECK(ram_gpioa.PUPDR == 0x64000000u);
    led_pa5_set(true);  CHECK(ram_gpioa.BSRR == 0x00000020u);
    led_pa5_set(false); CHECK(ram_gpioa.BSRR == 0x00200000u);

    fixture_reset(); usart2_pins_init();
    CHECK(ram_gpioa.MODER == 0xA80000A0u);                  /* PA2, PA3 = AF(10) */
    CHECK(ram_gpioa.AFR[0] == 0x00007700u);                 /* AFSEL2 = AFSEL3 = 7 */
    CHECK(ram_gpioa.AFR[1] == 0x00000000u);
    CHECK(ram_gpioa.OSPEEDR == 0x0C000010u);                /* PA2 medium */
    CHECK(ram_gpioa.PUPDR == 0x64000040u);                  /* PA3 풀업 */
    CHECK((ram_rcc.AHB1ENR & RCC_AHB1ENR_GPIOAEN) != 0u);
    led_pa5_init();
    CHECK(ram_gpioa.MODER == 0xA80004A0u);                  /* 순서와 무관하게 서로 안 건드림 */

    /* ---- Part C: 무작위 선채움 — 대상 필드 밖은 한 비트도 바뀌면 안 된다 ---- */
    {
        int bad_led = 0, bad_pins = 0;
        for (int i = 0; i < 500; i++) {
            uint32_t m = xs32(), ot = xs32(), sp = xs32(), pu = xs32(), af = xs32(), en = xs32();
            ram_gpioa.MODER = m; ram_gpioa.OTYPER = ot; ram_gpioa.OSPEEDR = sp;
            ram_gpioa.PUPDR = pu; ram_gpioa.AFR[0] = af; ram_rcc.AHB1ENR = en;
            led_pa5_init();
            if (ram_gpioa.MODER != ((m & ~0x00000C00u) | 0x00000400u) || ram_gpioa.OTYPER != (ot & ~0x20u) ||
                ram_gpioa.OSPEEDR != (sp & ~0x00000C00u) || ram_gpioa.PUPDR != (pu & ~0x00000C00u) ||
                ram_gpioa.AFR[0] != af || ram_rcc.AHB1ENR != (en | 1u)) bad_led++;
            ram_gpioa.MODER = m; ram_gpioa.OTYPER = ot; ram_gpioa.OSPEEDR = sp;
            ram_gpioa.PUPDR = pu; ram_gpioa.AFR[0] = af; ram_rcc.AHB1ENR = en;
            usart2_pins_init();
            if (ram_gpioa.MODER != ((m & ~0x000000F0u) | 0x000000A0u) || ram_gpioa.OTYPER != (ot & ~0x4u) ||
                ram_gpioa.OSPEEDR != ((sp & ~0x00000030u) | 0x00000010u) ||
                ram_gpioa.PUPDR != ((pu & ~0x000000F0u) | 0x00000040u) ||
                ram_gpioa.AFR[0] != ((af & ~0x0000FF00u) | 0x00007700u) || ram_rcc.AHB1ENR != (en | 1u)) bad_pins++;
        }
        CHECK(bad_led == 0);
        CHECK(bad_pins == 0);
    }

    /* ---- Part C: USART2 ---- */
    fixture_reset();
    CHECK(usart2_init(42000000u, 115200u) == 0);
    CHECK(ram_usart2.BRR == 0x16Du);                        /* 42e6/115200 = 364.58 → 365 = 22<<4 | 13 */
    CHECK(ram_usart2.CR1 == 0x0000200Cu);                   /* UE | TE | RE */
    CHECK(ram_usart2.CR2 == 0x00000000u);
    CHECK(ram_rcc.APB1ENR == 0x00020000u);                  /* USART2EN */
    fixture_reset();
    ram_usart2.CR1 = 0x00009620u;                           /* OVER8|M|PCE|PS|RXNEIE 가 켜진 더러운 상태 */
    ram_usart2.CR2 = 0x00003040u;                           /* STOP=11 + 다른 비트(bit6) */
    ram_rcc.APB1ENR = 0x10000001u;
    CHECK(usart2_init(16000000u, 115200u) == 0);
    CHECK(ram_usart2.BRR == 0x08Bu);                        /* 138.89 → 139 = 8<<4 | 11 */
    CHECK(ram_usart2.CR1 == 0x0000202Cu);                   /* 8N1·OVER8=0 로 정리, RXNEIE 는 남김 */
    CHECK(ram_usart2.CR2 == 0x00000040u);                   /* STOP 만 00 */
    CHECK(ram_rcc.APB1ENR == 0x10020001u);
    fixture_reset(); CHECK(usart2_init(16000000u, 9600u) == 0 && ram_usart2.BRR == 0x683u);
    fixture_reset(); CHECK(usart2_init(42000000u, 9600u) == 0 && ram_usart2.BRR == 0x1117u);
    /* 잘못된 인자: -1 이고 레지스터는 그대로 */
    fixture_reset(); ram_usart2.BRR = 0xDEADu;
    CHECK(usart2_init(42000000u, 0u) == -1);
    CHECK(usart2_init(16000000u, 2000000u) == -1);          /* BRR = 8 < 16 (OVER8=0 최대 fck/16) */
    CHECK(usart2_init(42000000u, 300u) == -1);              /* BRR = 140000 > 0xFFFF */
    CHECK(ram_usart2.BRR == 0xDEADu && ram_usart2.CR1 == 0u && ram_rcc.APB1ENR == 0u);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
