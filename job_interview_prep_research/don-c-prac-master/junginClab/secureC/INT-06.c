#if 0
// 위험한 코드
#include <stdio.h>

int main() {
    char c = 200;
    int i = 1000;

    printf("%d\n", c);
    printf("%d / %d = %d\n", i, c, i / c);
    return 0;
}
#endif 

#if 1
// 해결 방법
#include <stdio.h>

int main() {
    unsigned char c = 200;
    int i = 1000;

    printf("%d / %d = %d\n", i, c, i / c);
    return 0;
}
#endif 
