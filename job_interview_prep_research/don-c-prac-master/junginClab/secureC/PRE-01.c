#if 1
// 위험한 코드
#include <stdio.h>

#define SQR(x)  x * x

int main() {
    int result = SQR(2+3);    // 1 + 2 * 1 + 2
    printf("result = %d\n", result);

    return 0;
}
#endif

#if 0
// 해결 방법
#include <stdio.h>

#define SQR(x)  (x) * (x)

int main() {
    int result = SQR(1+2);    // (1 + 2) * (1 + 2)
    printf("result = %d\n", result);

    return 0;
}

#endif