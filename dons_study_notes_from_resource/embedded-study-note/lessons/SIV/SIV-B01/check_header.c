/* SIV-B01 — 생성된 spi0_regs.h 를 C 컴파일러로 한 번 더 검증한다 (defense in depth)
 *
 * python lint 가 기술서를 검사했어도, 실제로 펌웨어/테스트가 쓰는 건 생성된 헤더다.
 *  1) 컴파일 시간: _Static_assert 로 "사람이 스펙 PDF에서 확인한 값" 과 필드 겹침 여부를 고정
 *  2) 실행 시간  : 레지스터 기술 테이블(SPI0_REGS[])의 일관성 + GET/PREP 매크로 왕복
 * 다른 헤더로 바꿔 끼우려면  -DREGS_H_PATH='"/tmp/other.h"'  (C11 6.10.2p4 computed include)
 *
 *   python3 -I solution.py --emit spi0_regs.h
 *   cc -std=c11 -Wall -Wextra -Werror -O2 check_header.c -o /tmp/SIV-B01_hdr && /tmp/SIV-B01_hdr
 */
#include <stdint.h>
#include <stdio.h>

#ifndef REGS_H_PATH
#define REGS_H_PATH "spi0_regs.h"
#endif
#define SPI0_REGS_WANT_TABLE 1
#include REGS_H_PATH

/* ---- 컴파일 시간 검사: 틀리면 빌드가 깨진다 ---- */
_Static_assert(SPI0_REG_COUNT == 14u, "register count changed - review tests");
_Static_assert(SPI0_CTRL_OFFSET == 0x004u, "CTRL offset (datasheet 표 4-2)");
_Static_assert(SPI0_CTRL_RESET == 0x00070302u, "CTRL reset (datasheet 표 4-2)");
_Static_assert(SPI0_ID_RESET == 0x53500102u, "ID reset");
_Static_assert(SPI0_CTRL_CLKDIV_MASK == (((1u << SPI0_CTRL_CLKDIV_WIDTH) - 1u) << SPI0_CTRL_CLKDIV_SHIFT),
               "CLKDIV mask/shift/width inconsistent");
/* 필드가 서로 겹치지 않으면 (마스크의 합) == (마스크의 OR). 겹치면 자리올림 때문에 합이 더 크다. */
#define CTRL_FIELDS_SUM (0ull + SPI0_CTRL_EN_MASK + SPI0_CTRL_MSTR_MASK + SPI0_CTRL_CPOL_MASK + \
    SPI0_CTRL_CPHA_MASK + SPI0_CTRL_LSBF_MASK + SPI0_CTRL_LOOPBACK_MASK + SPI0_CTRL_CLKDIV_MASK + \
    SPI0_CTRL_FSIZE_MASK)
#define CTRL_FIELDS_OR (SPI0_CTRL_EN_MASK | SPI0_CTRL_MSTR_MASK | SPI0_CTRL_CPOL_MASK | \
    SPI0_CTRL_CPHA_MASK | SPI0_CTRL_LSBF_MASK | SPI0_CTRL_LOOPBACK_MASK | SPI0_CTRL_CLKDIV_MASK | \
    SPI0_CTRL_FSIZE_MASK)
_Static_assert(CTRL_FIELDS_SUM == CTRL_FIELDS_OR, "CTRL fields overlap");
_Static_assert((SPI0_CTRL_RESET & ~SPI0_CTRL_DEFINED_MASK) == 0u, "CTRL reset sets reserved bits");

/* ---- 실행 시간 검사 ---- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)

static int table_consistent(void) {
    for (unsigned i = 0; i < SPI0_REG_COUNT; i++) {
        const spi0_reg_desc_t *r = &SPI0_REGS[i];
        uint32_t acc[6] = { r->rw, r->ro, r->wo, r->w1c, r->w1s, r->rc };
        uint32_t seen = 0;
        for (unsigned k = 0; k < 6; k++) {
            if (seen & acc[k]) return 0;          /* 한 비트에 access 가 두 개 */
            seen |= acc[k];
        }
        if (seen != r->defined) return 0;         /* 정의된 비트는 모두 access 하나를 가진다 */
        if (r->reset & ~r->defined) return 0;     /* reserved 비트에 리셋값 1 금지 */
        if (r->vol & ~r->defined) return 0;
        if (r->offset % 4u || r->offset + 4u > SPI0_SIZE) return 0;
        if (i && r->offset <= SPI0_REGS[i - 1].offset) return 0;   /* 오프셋 오름차순, 중복 없음 */
    }
    return 1;
}

int main(void) {
    CHECK(table_consistent());
    CHECK(SPI0_CTRL_ADDR == 0x40013004u);
    CHECK(SPI0_CTRL_CLKDIV_GET(SPI0_CTRL_RESET) == 3u);
    CHECK(SPI0_CTRL_FSIZE_GET(SPI0_CTRL_RESET) == 7u);
    CHECK(SPI0_CTRL_CLKDIV_PREP(0x1Fu) == 0x00000F00u);            /* 넘치는 값은 잘린다 */
    CHECK(SPI0_ID_PART_GET(SPI0_ID_RESET) == SPI0_ID_PART_RESET);
    CHECK((SPI0_CTRL_EN_PREP(1) | SPI0_CTRL_MSTR_PREP(1) | SPI0_CTRL_CLKDIV_PREP(5) |
           SPI0_CTRL_FSIZE_PREP(15)) == 0x000F0503u);
    CHECK(SPI0_REGS[3].w1c == SPI0_INT_RAW_W1C_MASK && SPI0_INT_RAW_W1C_MASK == 0x7u);
    CHECK(SPI0_ERR_CNT_RC_MASK == SPI0_ERR_CNT_VOLATILE_MASK);

    printf("\n%-10s %-6s %-10s %-10s %-10s\n", "reg", "off", "reset", "defined", "vol");
    for (unsigned i = 0; i < SPI0_REG_COUNT; i++)
        printf("%-10s 0x%03X  0x%08X 0x%08X 0x%08X\n", SPI0_REGS[i].name, (unsigned)SPI0_REGS[i].offset,
               (unsigned)SPI0_REGS[i].reset, (unsigned)SPI0_REGS[i].defined, (unsigned)SPI0_REGS[i].vol);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
