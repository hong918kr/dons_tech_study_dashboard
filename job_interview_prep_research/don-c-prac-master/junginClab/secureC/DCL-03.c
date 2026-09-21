#if 0
#include <stdio.h>
#include <assert.h>

#pragma pack(1)
typedef struct _Packet {
    char cmd;
    int  len;
} Packet;

int main() {
    assert(sizeof(Packet) == 5);
    printf("sizeof(Packet)=%lu\n", sizeof(Packet));

    Packet data;
    // ...

    return 0;
}
#endif

#if 1
#include <stdio.h>

#define JOIN_AGAIN(x, y)    x##y
#define JOIN(x, y)  JOIN_AGAIN(x, y)
#define static_assert(e)    \
    typedef char JOIN(assertion_failed_at_line, __LINE__) [(e) ? 1 : -1]

#pragma pack(1)
typedef struct _Packet {
    char cmd;
    int  len;
}  Packet;

int main() {
    static_assert(sizeof(Packet) == 5);
    // typedef char assertion_failed_at_line37[1];
    static_assert(1 == 1);
    // typedef char assertion_failed_at_line39[1];
    Packet data;
    // ...

    return 0;
}

#endif