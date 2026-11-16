/* ENV: cross — arm-none-eabi-gcc only (CMake target build). Never built for the host. */
/* libc_min.c — GCC may emit calls to memset/memcpy even with -ffreestanding
 * (struct copies, zeroing loops). With -nostdlib and no newlib we provide them. */
#include <stddef.h>

void *memset(void *d, int c, size_t n)
{
    unsigned char *p = d;
    while (n--)
        *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *restrict d, const void *restrict s, size_t n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    while (n--)
        *p++ = *q++;
    return d;
}
