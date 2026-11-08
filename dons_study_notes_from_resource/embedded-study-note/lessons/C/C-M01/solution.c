/* C-M01 메모리 맵 레지스터 접근: volatile 포인터와 레지스터 구조체
 *
 * 가상 UART "VUART" 레지스터 맵 (STM32F4 USART 와 같은 앞 4개 + reserved 구간 + 읽기 전용 ID)
 *   0x00 SR   상태      bit7 TXE(송신 버퍼 빔), bit5 RXNE(수신 데이터 있음)
 *   0x04 DR   데이터    하위 8비트
 *   0x08 BRR  보레이트  분주값 = round(PCLK / baud), 16..0xFFFF
 *   0x0C CR1  제어      bit13 UE(UART enable), bit3 TE, bit2 RE
 *   0x10–0x1F reserved  (쓰지도 읽지도 않는다)
 *   0x20 ID   읽기 전용 0x56554152 ("VUAR")
 *   인스턴스 2개: VUART1 = BASE, VUART2 = BASE + 0x400  (주변장치 창 1 KB 간격)
 *
 * 실제 칩: #define VUART1_BASE 0x40011000u — "정수 상수 → 포인터" 캐스트로 접근한다.
 * 호스트(맥)에는 그 주소에 아무것도 없으므로(역참조하면 SIGSEGV), 1 KB 정렬된 정적 배열을
 * "주변장치 창(window)"으로 두고 그 배열의 주소 (uintptr_t)periph_window 를 BASE 로 쓴다.
 * 정수 → 포인터 변환은 구현 정의(C11 6.3.2.3p5)다. 이 호스트 가짜는 "모델"일 뿐이다.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* CMSIS 의 __I / __O / __IO 와 같은 뜻. (밑줄 두 개 이름은 구현 예약어라 여기선 다른 이름을 쓴다) */
#define REG_RO volatile const   /* 읽기 전용 (CMSIS __I, __IM)      */
#define REG_WO volatile         /* 쓰기 전용 (CMSIS __O, __OM) — C 로는 표현 불가 */
#define REG_RW volatile         /* 읽기/쓰기 (CMSIS __IO, __IOM)    */

/* ---------------- 호스트 모델: 주변장치 창 (고치지 않는다) ---------------- */
#define VUART_STRIDE   0x400u
#define VUART_NINST    2u
#define VUART_ID_VALUE 0x56554152u
static _Alignas(1024) uint32_t periph_window[VUART_NINST * VUART_STRIDE / 4u];
#define VUART1_BASE ((uintptr_t)periph_window)          /* 타깃: 0x40011000u */
#define VUART2_BASE (VUART1_BASE + VUART_STRIDE)        /* 타깃: 0x40011400u */

/* 데이터시트의 오프셋 표 */
#define VUART_SR_OFS   0x00u
#define VUART_DR_OFS   0x04u
#define VUART_BRR_OFS  0x08u
#define VUART_CR1_OFS  0x0Cu
#define VUART_ID_OFS   0x20u

#define VUART_SR_RXNE  (1u << 5)
#define VUART_SR_TXE   (1u << 7)
#define VUART_CR1_RE   (1u << 2)
#define VUART_CR1_TE   (1u << 3)
#define VUART_CR1_UE   (1u << 13)
#define VUART_BRR_MIN  16u
#define VUART_BRR_MAX  0xFFFFu

/* ---------------- Part A-1: #define 주소 방식 ---------------- */
/* 주소(정수)를 "레지스터 lvalue" 로: 정수 → volatile 포인터 → 역참조 */
#define REG32(addr)    (*(volatile uint32_t *)(uintptr_t)(addr))
/* 읽기 전용: const volatile lvalue 라 대입하면 컴파일 에러 */
#define REG32_RO(addr) (*(const volatile uint32_t *)(uintptr_t)(addr))

#define VUART1_SR  REG32(VUART1_BASE + VUART_SR_OFS)
#define VUART1_DR  REG32(VUART1_BASE + VUART_DR_OFS)
#define VUART1_BRR REG32(VUART1_BASE + VUART_BRR_OFS)
#define VUART1_CR1 REG32(VUART1_BASE + VUART_CR1_OFS)
#define VUART1_ID  REG32_RO(VUART1_BASE + VUART_ID_OFS)

/* ---------------- Part A-2: 구조체 오버레이 방식 ---------------- */
typedef struct {
    REG_RW uint32_t SR;            /* 0x00 */
    REG_RW uint32_t DR;            /* 0x04 */
    REG_RW uint32_t BRR;           /* 0x08 */
    REG_RW uint32_t CR1;           /* 0x0C */
    uint32_t        RESERVED0[(0x20u - 0x10u) / 4u];  /* 0x10–0x1F: 4 워드 */
    REG_RO uint32_t ID;            /* 0x20, 읽기 전용 */
} VUART_TypeDef;

#define VUART1 ((VUART_TypeDef *)VUART1_BASE)   /* 타깃: ((VUART_TypeDef *)0x40011000u) */
#define VUART2 ((VUART_TypeDef *)VUART2_BASE)

/* ---------------- Part A-3: 레이아웃을 컴파일 타임에 고정 ---------------- */
_Static_assert(offsetof(VUART_TypeDef, SR)  == VUART_SR_OFS,  "SR  must be at 0x00");
_Static_assert(offsetof(VUART_TypeDef, DR)  == VUART_DR_OFS,  "DR  must be at 0x04");
_Static_assert(offsetof(VUART_TypeDef, BRR) == VUART_BRR_OFS, "BRR must be at 0x08");
_Static_assert(offsetof(VUART_TypeDef, CR1) == VUART_CR1_OFS, "CR1 must be at 0x0C");
_Static_assert(offsetof(VUART_TypeDef, ID)  == VUART_ID_OFS,  "ID  must be at 0x20");
_Static_assert(sizeof(VUART_TypeDef) == 0x24u && sizeof(VUART_TypeDef) <= VUART_STRIDE, "VUART block size");

/* ---------------- Part B-1: 레지스터 포인터 → 데이터시트 오프셋 ---------------- */
/* 디버그 로그용: "VUART2 +0x0C 에 썼다" 처럼 출력하려면 바이트 오프셋이 필요하다 */
static uint32_t vuart_reg_offset(const VUART_TypeDef *u, const volatile uint32_t *reg) {
    return (uint32_t)((uintptr_t)reg - (uintptr_t)u);   /* 정수 주소의 차 = 바이트 오프셋 */
}

/* ---------------- Part B-2: 16비트 레지스터 정의 (예전 STM32 SPL 헤더 스타일) ---------------- */
/* 같은 맵을 16비트 멤버로 정의한다. 각 레지스터의 상위 하프워드는 reserved 다. */
typedef struct {
    REG_RW uint16_t SR;            /* 0x00 */
    uint16_t        RESERVED0;     /* 0x02 */
    REG_RW uint16_t DR;            /* 0x04 */
    uint16_t        RESERVED1;     /* 0x06 */
    REG_RW uint16_t BRR;           /* 0x08 */
    uint16_t        RESERVED2;     /* 0x0A */
    REG_RW uint16_t CR1;           /* 0x0C */
    uint16_t        RESERVED3;     /* 0x0E */
} VUART16_TypeDef;
_Static_assert(offsetof(VUART16_TypeDef, DR)  == 0x04u, "DR16  must be at 0x04");
_Static_assert(offsetof(VUART16_TypeDef, BRR) == 0x08u, "BRR16 must be at 0x08");
_Static_assert(offsetof(VUART16_TypeDef, CR1) == 0x0Cu, "CR1_16 must be at 0x0C");
_Static_assert(sizeof(VUART16_TypeDef) == 0x10u, "VUART16 block size");

static _Alignas(4) uint16_t periph16_window[0x10u / 2u];   /* 16비트 모델용 창 (고치지 않는다) */
#define VUART16 ((VUART16_TypeDef *)(uintptr_t)periph16_window)

/* ---------------- Part B-3: 인스턴스 번호 ↔ 베이스 주소 ---------------- */
/* i = 0 → VUART1, 1 → VUART2, 그 밖 → NULL */
static VUART_TypeDef *vuart_by_index(unsigned i) {
    if (i >= VUART_NINST) return NULL;
    return (VUART_TypeDef *)(VUART1_BASE + (uintptr_t)i * VUART_STRIDE);   /* 정수로 계산 후 캐스트 */
}
/* 인스턴스 베이스면 그 번호, 아니면(NULL, 블록 중간 주소, 범위 밖) -1 */
static int vuart_index_of(const VUART_TypeDef *u) {
    uintptr_t a = (uintptr_t)u;
    if (a < VUART1_BASE) return -1;                       /* NULL 포함 */
    uintptr_t d = a - VUART1_BASE;
    if (d % VUART_STRIDE != 0u || d / VUART_STRIDE >= VUART_NINST) return -1;
    return (int)(d / VUART_STRIDE);
}

/* ---------------- Part C: 오버레이 위의 작은 드라이버 ---------------- */
/* BRR = round(pclk/baud). baud == 0 이거나 분주값이 16..0xFFFF 밖이면 레지스터를 건드리지 않고 -1.
 * 부트로더가 UART 를 켜 둔 상태일 수 있다: 끄고(CR1=0) → BRR → TE|RE → 마지막에 UE. */
static int uart_init(VUART_TypeDef *u, uint32_t pclk_hz, uint32_t baud) {
    if (baud == 0u) return -1;
    uint64_t div = ((uint64_t)pclk_hz + baud / 2u) / baud;   /* 64비트로 반올림: 덧셈 넘침 방지 */
    if (div < VUART_BRR_MIN || div > VUART_BRR_MAX) return -1;
    u->CR1 = 0u;                                   /* RMW 가 아니라 통째로 쓴다: 남은 비트 제거 */
    u->BRR = (uint32_t)div;
    u->CR1 = VUART_CR1_TE | VUART_CR1_RE;
    u->CR1 = VUART_CR1_TE | VUART_CR1_RE | VUART_CR1_UE;   /* enable 은 마지막 */
    return 0;
}
/* SR.TXE 를 최대 max_polls 번 확인, 서면 DR 에 쓰고 0. 끝내 안 서면 DR 을 건드리지 않고 -1. */
static int uart_putc(VUART_TypeDef *u, uint8_t c, int max_polls) {
    for (int i = 0; i < max_polls; i++) {
        if (u->SR & VUART_SR_TXE) {                /* volatile: 매 반복 다시 읽는다 */
            u->DR = c;
            return 0;
        }
    }
    return -1;
}
/* SR.RXNE 면 DR 하위 8비트(0..255), 아니면 -1. SR 1번, DR 최대 1번 읽는다. */
static int uart_getc(VUART_TypeDef *u) {
    if (!(u->SR & VUART_SR_RXNE)) return -1;
    return (int)(u->DR & 0xFFu);                   /* DR 읽기 1번 (실제 칩에선 RXNE 클리어) */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* lvalue 의 타입 검사: &(lvalue) 의 포인터 타입으로 한정자를 본다 */
#define IS_V32(lv)  _Generic(&(lv), volatile uint32_t *: 1, default: 0)
#define IS_CV32(lv) _Generic(&(lv), const volatile uint32_t *: 1, default: 0)
#define IS_V16(lv)  _Generic(&(lv), volatile uint16_t *: 1, default: 0)
/* 하드웨어 쪽에서 본 원시 메모리: 인스턴스 inst 의 바이트 오프셋 ofs 워드 */
#define WIN(inst, ofs) periph_window[((inst) * VUART_STRIDE + (ofs)) / 4u]

#define FILL 0xA5A5A5A5u
static void sim_reset(uint32_t fill) {   /* 리셋 상태 모델: 전부 fill, ID 만 하드웨어 값 */
    for (size_t i = 0; i < sizeof periph_window / sizeof periph_window[0]; i++) periph_window[i] = fill;
    for (unsigned k = 0; k < VUART_NINST; k++) WIN(k, VUART_ID_OFS) = VUART_ID_VALUE;
    for (size_t i = 0; i < sizeof periph16_window / sizeof periph16_window[0]; i++) periph16_window[i] = 0;
}
static int untouched(unsigned inst, uint32_t from, uint32_t to) {   /* [from,to) 바이트 구간이 FILL 그대로? */
    for (uint32_t o = from; o < to; o += 4u)
        if (WIN(inst, o) != FILL) return 0;
    return 1;
}

int main(void) {
    /* ===== Part A: 두 방식이 같은 바이트를 가리키는가 ===== */
    sim_reset(0);
    CHECK(IS_V32(VUART1_DR));                          /* REG32 lvalue 는 volatile */
    CHECK(IS_CV32(VUART1_ID));                         /* REG32_RO lvalue 는 const volatile */
    CHECK(IS_V32(VUART1->CR1));
    CHECK(IS_CV32(VUART1->ID));                        /* 구조체의 ID 도 읽기 전용 */
    CHECK(offsetof(VUART_TypeDef, SR)  == VUART_SR_OFS);
    CHECK(offsetof(VUART_TypeDef, DR)  == VUART_DR_OFS);
    CHECK(offsetof(VUART_TypeDef, BRR) == VUART_BRR_OFS);
    CHECK(offsetof(VUART_TypeDef, CR1) == VUART_CR1_OFS);
    CHECK(offsetof(VUART_TypeDef, ID)  == VUART_ID_OFS);
    CHECK(sizeof(VUART_TypeDef) == 0x24u);
    CHECK((VUART1_BASE & (VUART_STRIDE - 1u)) == 0u);  /* 창은 1 KB 정렬 */
    CHECK(&VUART1_DR == &VUART1->DR);                  /* 두 방식이 같은 주소 */
    CHECK((uintptr_t)&VUART1->ID == VUART1_BASE + VUART_ID_OFS);
    VUART1_BRR = 0x8Bu;                                /* #define 으로 쓰고 → 구조체·원시 메모리로 읽기 */
    CHECK(VUART1->BRR == 0x8Bu && WIN(0, VUART_BRR_OFS) == 0x8Bu);
    VUART1->CR1 = 0x200Cu;                             /* 구조체로 쓰고 → #define 으로 읽기 */
    CHECK(VUART1_CR1 == 0x200Cu);
    CHECK(VUART1_ID == VUART_ID_VALUE);
    CHECK(VUART1->ID == VUART_ID_VALUE);               /* 오버레이가 0x20 을 읽어야 한다 */
    VUART2->BRR = 0x2D9u;                              /* 인스턴스끼리 독립 */
    CHECK(VUART1->BRR == 0x8Bu && WIN(1, VUART_BRR_OFS) == 0x2D9u);

    /* ===== Part B-1: 포인터 → 바이트 오프셋 ===== */
    CHECK(vuart_reg_offset(VUART1, &VUART1->SR) == 0x00u);
    CHECK(vuart_reg_offset(VUART1, &VUART1->CR1) == 0x0Cu);
    CHECK(vuart_reg_offset(VUART2, &VUART2->ID) == 0x20u);

    /* ===== Part B-2: 16비트 정의와 접근 폭 ===== */
    CHECK(IS_V16(VUART16->DR));
    CHECK(offsetof(VUART16_TypeDef, DR)  == 0x04u);
    CHECK(offsetof(VUART16_TypeDef, BRR) == 0x08u);
    CHECK(offsetof(VUART16_TypeDef, CR1) == 0x0Cu);
    CHECK(sizeof(VUART16_TypeDef) == 0x10u);
    periph16_window[3] = 0xFFFFu;                      /* DR 워드의 상위 하프워드 (reserved) */
    VUART16->DR = 0x41u;                               /* 16비트 쓰기는 하위 하프워드만 */
    CHECK(periph16_window[2] == 0x41u && periph16_window[3] == 0xFFFFu && periph16_window[1] == 0u);

    /* ===== Part B-3: 인스턴스 번호 ↔ 베이스 ===== */
    CHECK(vuart_by_index(0) == VUART1);
    CHECK(vuart_by_index(1) == VUART2);
    CHECK(vuart_by_index(2) == NULL);
    CHECK(vuart_index_of(VUART1) == 0);
    CHECK(vuart_index_of(VUART2) == 1);
    CHECK(vuart_index_of((const VUART_TypeDef *)(VUART1_BASE + 4u)) == -1);   /* 블록 중간 */
    CHECK(vuart_index_of(NULL) == -1);

    /* ===== Part C: 드라이버 ===== */
    sim_reset(FILL);                                   /* "부트로더가 남긴" 알 수 없는 값 */
    CHECK(uart_init(VUART1, 16000000u, 115200u) == 0);
    CHECK(WIN(0, VUART_BRR_OFS) == 139u);              /* 138.89 → 반올림 139 (0x8B) */
    CHECK(VUART1_CR1 == (VUART_CR1_UE | VUART_CR1_TE | VUART_CR1_RE));   /* 쓰레기 비트 없이 정확히 */
    CHECK(untouched(0, 0x10u, 0x20u) && WIN(0, VUART_ID_OFS) == VUART_ID_VALUE);  /* reserved·ID 그대로 */
    CHECK(untouched(1, 0x00u, 0x20u));                 /* VUART2 는 한 워드도 안 건드림 */
    CHECK(uart_init(VUART2, 42000000u, 9600u) == 0 && VUART2->BRR == 4375u);
    sim_reset(FILL);
    CHECK(uart_init(VUART1, 16000000u, 0u) == -1 && untouched(0, 0x00u, 0x20u));        /* baud 0 */
    CHECK(uart_init(VUART1, 1000000u, 115200u) == -1 && untouched(0, 0x00u, 0x20u));    /* 분주 9 < 16 */
    CHECK(uart_init(VUART1, 16000000u, 200u) == -1 && untouched(0, 0x00u, 0x20u));      /* 80000 > 0xFFFF */
    CHECK(uart_init(VUART1, 4294000000u, 2000000u) == 0 && VUART1->BRR == 2147u);       /* 32비트 넘침 주의 */

    sim_reset(0);
    VUART1->SR = VUART_SR_TXE;
    CHECK(uart_putc(VUART1, 'A', 3) == 0 && VUART1_DR == 'A');
    VUART1->SR = 0u;
    VUART1->DR = 0x55u;
    CHECK(uart_putc(VUART1, 'B', 3) == -1 && VUART1_DR == 0x55u);   /* 타임아웃: DR 그대로 */
    VUART1->SR = VUART_SR_RXNE;
    VUART1->DR = 0x1A5u;                               /* 하위 8비트만 데이터 */
    CHECK(uart_getc(VUART1) == 0xA5);
    VUART1->SR = 0u;
    CHECK(uart_getc(VUART1) == -1);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
