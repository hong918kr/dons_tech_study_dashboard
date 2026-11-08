/* demo_def_include.c — 같은 목록을 "매크로 인자" 대신 ".def 파일 재포함" 으로 쓰는 방식 (LLVM *.def 스타일)
 *   cc -std=c11 -Wall -Wextra demo_def_include.c -o demo_def_include && ./demo_def_include
 *   cc -std=c11 -Wall -Wextra -DFORGET demo_def_include.c     # 정의 없이 포함 → #error
 */
#include <stdint.h>
#include <stdio.h>

enum reg_acc { ACC_RO, ACC_RW, ACC_WO, ACC_RC, ACC_W1C };

#define UART_REG(name, ofs, rst, acc) UART_REG_##name,
enum uart_reg_id {
#include "uart_regs.def"          /* .def 끝의 #undef UART_REG 가 매번 정의를 지워 준다 */
    UART_REG_COUNT
};

#define UART_REG(name, ofs, rst, acc) { #name, (ofs), (rst) },
static const struct { const char *name; uint32_t ofs, rst; } k_regs[] = {
#include "uart_regs.def"
};

#ifdef FORGET
#include "uart_regs.def"
#endif

int main(void) {
    printf("UART_REG_COUNT=%d table=%zu\n", (int)UART_REG_COUNT, sizeof k_regs / sizeof k_regs[0]);
    for (int i = 0; i < UART_REG_COUNT; i++)
        printf("%-4s @0x%02X reset 0x%08X\n", k_regs[i].name, (unsigned)k_regs[i].ofs, (unsigned)k_regs[i].rst);
    return 0;
}
