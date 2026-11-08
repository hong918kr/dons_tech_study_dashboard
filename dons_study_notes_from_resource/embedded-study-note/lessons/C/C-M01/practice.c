/* C-M01 추가 연습 — 한 번에 검증 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 본문과 같은 VUART 오버레이 (호스트 모델: 정렬된 정적 배열이 주변장치 창) */
typedef struct {
    volatile uint32_t SR, DR, BRR, CR1;          /* 0x00 0x04 0x08 0x0C */
    uint32_t RESERVED0[4];                       /* 0x10–0x1F */
    volatile const uint32_t ID;                  /* 0x20 */
} VUART_TypeDef;
_Static_assert(offsetof(VUART_TypeDef, ID) == 0x20u, "ID at 0x20");
static _Alignas(1024) uint32_t win[0x24u / 4u];
#define VUART1 ((VUART_TypeDef *)(uintptr_t)win)

/* 연습 1: 한정자는 어디에 붙나 — 멤버가 일반 타입인 구조체를 volatile 로 접근하기 */
typedef struct { uint32_t SR, DR; } NV_TypeDef;   /* 멤버에 volatile 없음 */
#define PTYPE(lv) _Generic(&(lv),                  \
        uint32_t *: 0, volatile uint32_t *: 1,     \
        const volatile uint32_t *: 2, default: 9)

/* 연습 2: STM32F4 USART BRR (OVER8=0) — 가수(mantissa) 12비트 + 소수(fraction) 4비트 */
static uint32_t brr_f4(uint32_t pclk, uint32_t baud) {
    return (uint32_t)(((uint64_t)pclk + baud / 2u) / baud);    /* = round(16 * USARTDIV) */
}
static int32_t baud_error_ppm(uint32_t pclk, uint32_t baud) {  /* (실제 - 목표) / 목표 × 1e6, 0 쪽 버림 */
    uint32_t brr = brr_f4(pclk, baud);
    int64_t actual_u = (int64_t)pclk * 1000000 / brr;          /* 실제 보레이트 × 1e6 */
    return (int32_t)((actual_u - (int64_t)baud * 1000000) / baud);
}

/* 연습 3: Cortex-M3/M4 비트밴드(bit-band) 별칭 주소 — 순수 정수 계산 */
static uint32_t bb_alias(uint32_t addr, unsigned bit) {
    if (bit > 31u) return 0u;
    if (addr >= 0x20000000u && addr <= 0x200FFFFFu)          /* SRAM 비트밴드 영역 1 MB */
        return 0x22000000u + (addr - 0x20000000u) * 32u + bit * 4u;
    if (addr >= 0x40000000u && addr <= 0x400FFFFFu)          /* 주변장치 비트밴드 영역 1 MB */
        return 0x42000000u + (addr - 0x40000000u) * 32u + bit * 4u;
    return 0u;                                               /* 비트밴드 영역 아님 */
}

/* 연습 4: offsetof 표로 레지스터 덤프 (디버그 셸 "uart dump" 명령) */
static const struct { const char *name; size_t ofs; } vuart_regs[] = {
    { "SR",  offsetof(VUART_TypeDef, SR)  }, { "DR",  offsetof(VUART_TypeDef, DR)  },
    { "BRR", offsetof(VUART_TypeDef, BRR) }, { "CR1", offsetof(VUART_TypeDef, CR1) },
    { "ID",  offsetof(VUART_TypeDef, ID)  },
};
static size_t vuart_dump(const VUART_TypeDef *u, char *out, size_t cap) {
    size_t n = 0;
    out[0] = '\0';
    for (size_t i = 0; i < sizeof vuart_regs / sizeof vuart_regs[0]; i++) {
        uint32_t v = *(const volatile uint32_t *)((uintptr_t)u + vuart_regs[i].ofs);  /* 읽기 1번씩 */
        int k = snprintf(out + n, cap - n, "%s%s=%08X", i ? " " : "", vuart_regs[i].name, (unsigned)v);
        if (k < 0 || (size_t)k >= cap - n) return 0;          /* 잘림 */
        n += (size_t)k;
    }
    return n;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* 연습 1 */
    static NV_TypeDef nv;
    volatile NV_TypeDef *p = &nv;             /* 가리키는 구조체가 volatile → 멤버 접근도 volatile */
    NV_TypeDef *volatile q = &nv;             /* 포인터 변수만 volatile → 멤버 접근은 일반 */
    const VUART_TypeDef *c = VUART1;          /* const 구조체 + volatile 멤버 → const volatile */
    CHECK(PTYPE(p->DR) == 1);
    CHECK(PTYPE(q->DR) == 0);
    CHECK(PTYPE(c->DR) == 2);
    CHECK(PTYPE(VUART1->ID) == 2);

    /* 연습 2 */
    CHECK(brr_f4(84000000u, 115200u) == 0x2D9u);             /* 729 */
    CHECK((brr_f4(84000000u, 115200u) >> 4) == 45u && (brr_f4(84000000u, 115200u) & 0xFu) == 9u);
    CHECK(brr_f4(16000000u, 115200u) == 0x8Bu);              /* 139: 가수 8, 소수 11 */
    CHECK(baud_error_ppm(84000000u, 115200u) == 228);        /* +0.023 % */
    CHECK(baud_error_ppm(16000000u, 115200u) == -799);       /* -0.080 % */
    CHECK(baud_error_ppm(16000000u, 9600u) == -199);         /* 1666.67 → 1667: 16e6/1667 = 9598.08 */
    CHECK(baud_error_ppm(42000000u, 9600u) == 0);            /* 4375 로 딱 나누어떨어짐 */

    /* 연습 3 */
    CHECK(bb_alias(0x40011000u, 7u) == 0x4222001Cu);         /* STM32F4 USART1->SR.TXE */
    CHECK(bb_alias(0x20000300u, 2u) == 0x22006008u);
    CHECK(bb_alias(0x400FFFFCu, 31u) == 0x43FFFFFCu);        /* 영역 끝 */
    CHECK(bb_alias(0x40100000u, 0u) == 0u);                  /* 영역 밖 */
    CHECK(bb_alias(0x08000000u, 0u) == 0u);                  /* 플래시: 비트밴드 없음 */

    /* 연습 4 */
    char buf[96];
    memset(win, 0, sizeof win);
    win[0] = 0x80u; win[1] = 0x41u; win[2] = 0x8Bu; win[3] = 0x200Cu; win[8] = 0x56554152u;
    win[4] = 0xDEADBEEFu;                                     /* reserved 는 덤프에 안 나온다 */
    CHECK(vuart_dump(VUART1, buf, sizeof buf) > 0);
    CHECK(strcmp(buf, "SR=00000080 DR=00000041 BRR=0000008B CR1=0000200C ID=56554152") == 0);
    CHECK(vuart_dump(VUART1, buf, 20) == 0);                  /* 버퍼 부족 → 0 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
