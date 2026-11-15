/* RTOS-M08 freestanding stub (thumbv7em-none-eabi 에는 libc 헤더가 없다): 커널이 쓰는 선언만 */
#include <stddef.h>
void *memset(void *, int, size_t);
void *memcpy(void *, const void *, size_t);
size_t strlen(const char *);
