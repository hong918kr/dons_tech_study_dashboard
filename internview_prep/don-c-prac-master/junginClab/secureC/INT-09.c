#if 0
// 위험한 코드
#include <stdio.h>

int main() {
    char packet = 0x80;  

    printf("%x", packet >> 4); // 0xFFFFFF80
    // 10000000 => 00001000
    // 10000000 => 11111000
    //               0xF8
    return 0;
}

#endif

#if 1
// 해결 방법
#include <stdio.h>

int main() {
    unsigned char packet = 0x80;

    printf("%#x", packet >> 4); // 0x8
    return 0;
}
#endif
