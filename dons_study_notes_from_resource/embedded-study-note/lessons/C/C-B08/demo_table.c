/* C-B08 demo: 큰 룩업 테이블의 const 유무 → 섹션 이동 관찰
 *   mac (Mach-O):   cc -std=c11 -O2 -c demo_table.c -o t_data.o
 *                   cc -std=c11 -O2 -c -DUSE_CONST demo_table.c -o t_const.o
 *                   size -m t_data.o ; nm -m t_data.o      (const 판도 같은 식으로)
 *   Cortex-M (ELF): cc --target=thumbv7em-none-eabi -mcpu=cortex-m4 -ffreestanding -O2 -c [-DUSE_CONST] demo_table.c
 *                   llvm-size / llvm-size -A / nm
 */
#include <stdint.h>

#ifdef USE_CONST
#define TABLE_QUAL const
#else
#define TABLE_QUAL
#endif

/* 1024개 x 4바이트 = 4096바이트 테이블 (값은 아무거나: 곱셈 해시) */
#define V(i)     ((uint32_t)(i) * 2654435761u)
#define R4(i)    V(i), V((i) + 1), V((i) + 2), V((i) + 3)
#define R16(i)   R4(i), R4((i) + 4), R4((i) + 8), R4((i) + 12)
#define R64(i)   R16(i), R16((i) + 16), R16((i) + 32), R16((i) + 48)
#define R256(i)  R64(i), R64((i) + 64), R64((i) + 128), R64((i) + 192)
#define R1024(i) R256(i), R256((i) + 256), R256((i) + 512), R256((i) + 768)

TABLE_QUAL uint32_t big_table[1024] = { R1024(0) };

/* 포인터 표: 문자열도 슬롯도 const (const T* const). 단 각 슬롯에 "주소"가 들어간다 → 재배치(relocation) 필요 */
const char *const names[] = { "reset", "status", "write", "erase" };

uint32_t lookup(unsigned i);
uint32_t lookup(unsigned i) { return big_table[i & 1023u]; }

const char *name_of(unsigned i);
const char *name_of(unsigned i) { return names[i & 3u]; }
