/* C-B13 demo: 여러 타입의 메모리 배치를 바이트 단위로 본다 */
#include <stdint.h>
#include <stdio.h>

static void dump(const char *name, const void *p, size_t n) {
    const unsigned char *b = p;
    printf("%-22s @%p :", name, p);
    for (size_t i = 0; i < n; i++) printf(" %02x", b[i]);
    printf("\n");
}

int main(void) {
    union { uint32_t u; uint8_t b[4]; } pun = { .u = 0x01020304u };
    printf("__BYTE_ORDER__ == %s\n",
           __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ ? "__ORDER_LITTLE_ENDIAN__" : "__ORDER_BIG_ENDIAN__");
    printf("union pun: b[0]=0x%02x -> %s endian\n", pun.b[0], pun.b[0] == 4 ? "little" : "big");

    uint16_t h = 0xABCD;             dump("uint16_t 0xABCD", &h, sizeof h);
    uint32_t w = 0x11223344;         dump("uint32_t 0x11223344", &w, sizeof w);
    uint64_t d = 0x0102030405060708; dump("uint64_t 0x0102..08", &d, sizeof d);
    int32_t  m = -2;                 dump("int32_t -2", &m, sizeof m);
    float    f = 1.0f;               dump("float 1.0f", &f, sizeof f);
    char     s[] = "ABCD";           dump("char[] \"ABCD\"", s, 4);
    return 0;
}
