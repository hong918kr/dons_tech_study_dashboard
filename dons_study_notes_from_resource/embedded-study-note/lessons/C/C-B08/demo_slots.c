/* C-B08 demo: const 를 "어디에" 붙였느냐에 따라 플래시/RAM/스택이 갈린다 (Cortex-M 오브젝트로 관찰)
 *   cc --target=thumbv7em-none-eabi -mcpu=cortex-m4 -ffreestanding -std=c11 -O2 -c demo_slots.c && nm demo_slots.o
 *   cc --target=thumbv7em-none-eabi -mcpu=cortex-m4 -ffreestanding -std=c11 -O2 -S demo_slots.c -o -   # use_local 의 ldm/stm 복사
 */
#include <stdint.h>
const char *names_rw[] = { "reset", "status" };
const char *const names_ro[] = { "reset", "status" };
void sink(const uint8_t *p);
void use_local(void) {
    const uint8_t tbl[64] = { 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32 };
    sink(tbl);
}
void use_static(void) {
    static const uint8_t tbl[64] = { 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32 };
    sink(tbl);
}
